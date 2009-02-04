#include "googledataresource.h"

#include "settings.h"
#include "settingsadaptor.h"

#include <QtDBus/QDBusConnection>
#include <kabc/addressee.h>
#include <kabc/phonenumber.h>
#include <kabc/key.h>
#include <kabc/errorhandler.h>
#include <qstring.h>
#include <KWindowSystem>

extern "C" {
#include <gcalendar.h>
#include <gcontact.h>
#include <gcal_status.h>
}


/** FIXME: for some reason the 'retrieveItem' functions is not being called.
 * this makes the entries to lack its contents (name, email, etc).
 * I should investigate why and fix, for while this is a workaround:
 * I report the payload in the 'retrieveItems' function.
 */
#define ITEM_BUG_WTF

using namespace Akonadi;

GoogleDataResource::GoogleDataResource( const QString &id )
	: ResourceBase(id), dlgConf(0), authenticated(false)
{
	new SettingsAdaptor( Settings::self() );
	QDBusConnection::sessionBus().registerObject(
		QLatin1String( "/Settings" ), Settings::self(),
		QDBusConnection::ExportAdaptors );


	if (!(gcal = gcal_new(GCONTACT)))
		exit(1);
	gcal_set_store_xml(gcal, 1);
}

GoogleDataResource::~GoogleDataResource()
{
	gcal_delete(gcal);
	gcal_cleanup_contacts(&all_contacts);
	if (dlgConf)
		delete dlgConf;
}

void GoogleDataResource::retrieveCollections()
{
	if (!authenticated) {
		kError() << "No athentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "No yet authenticated for"
					      " use of Google Contacts");
		emit error(message);

		emit status(Broken, message);
		return;
	}

	Collection c;
	c.setParent(Collection::root());
	c.setRemoteId("google-contacts");
	c.setName(name());

	QStringList mimeTypes;
	mimeTypes << "text/directory";
	c.setContentMimeTypes(mimeTypes);

	Collection::List list;
	list << c;
	collectionsRetrieved(list);

}

void GoogleDataResource::retrieveItems( const Akonadi::Collection &collection )
{
	Q_UNUSED( collection );

	Item::List items;
	int result;

	if (!authenticated) {
		kError() << "No athentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "No  yet authenticated for"
					      " use of Google Contacts");
		emit error(message);

		emit status(Broken, message);
		return;
	}

	/* Downloading the contacts can be slow and it is blocking. Will
	 * it mess up with akonadi?
	 */
	if ((result = gcal_get_contacts(gcal, &all_contacts)))
		exit(1);

	/* Each google entry has a unique ID and edit_url */
	for (size_t i = 0; i < all_contacts.length; ++i) {
		Item item(QLatin1String("text/directory"));
		gcal_contact_t contact = gcal_contact_element(&all_contacts, i);

#ifdef ITEM_BUG_WTF
		KABC::Addressee addressee;
		KABC::PhoneNumber number;
		KABC::Key key;
		QString temp;

		/* name */
		temp = gcal_contact_get_title(contact);
		addressee.setNameFromString(temp);
		/* email */
		temp = gcal_contact_get_email(contact);
		addressee.insertEmail(temp, true);
		/* edit url: required to do edit/delete */
		temp = gcal_contact_get_url(contact);
		addressee.setUid(temp);
		/* ETag: required by Google Data protocol 2.0 */
		temp = gcal_contact_get_etag(contact);
		key.setId(temp);
		addressee.insertKey(key);
		/* TODO: telefone, address, etc */

		item.setPayload<KABC::Addressee>(addressee);
#endif

		item.setRemoteId(gcal_contact_get_id(contact));


		items << item;
	}

	itemsRetrieved(items);
}

bool GoogleDataResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED( parts );
	const QString entry_id = item.remoteId();
	QString temp;
	Item newItem(item);
	gcal_contact_t contact;
	KABC::Addressee addressee;
	KABC::PhoneNumber number;
	KABC::Key key;

	if (!authenticated) {
		kError() << "No athentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "No yet authenticated for"
					      " use of Google Contacts");
		emit error(message);

		emit status(Broken, message);
		return false;
	}

	/*
	 * And another question, are the requests in the same sequence that
	 * I informed in 'retrieveItems'? For while, I try to locate the entry...
	 */
	for (size_t i = 0; i < all_contacts.length; ++i) {
		contact = gcal_contact_element(&all_contacts, i);
		if (entry_id == gcal_contact_get_id(contact)) {
			/* name */
			temp = gcal_contact_get_title(contact);
			addressee.setNameFromString(temp);

			/* email */
			temp = gcal_contact_get_email(contact);
			addressee.insertEmail(temp, true);

			/* edit url: required to do edit/delete */
			temp = gcal_contact_get_url(contact);
			addressee.setUid(temp);

			/* ETag: required by Google Data protocol 2.0 */
			temp = gcal_contact_get_etag(contact);
			key.setId(temp);
			addressee.insertKey(key);

			/* TODO: telefone, address, etc */

			newItem.setPayload<KABC::Addressee>(addressee);

                        itemRetrieved(newItem);
			return true;
		}

	}

	return false;
}

void GoogleDataResource::aboutToQuit()
{
	// TODO: any cleanup you need to do while there is still an active
	// event loop. The resource will terminate after this method returns
}

void GoogleDataResource::configure( WId windowId )
{
	Q_UNUSED( windowId );
	char *user, *pass;
	int result = -1;
	QByteArray byteUser, bytePass;

	if (!dlgConf)
		dlgConf = new dlgGoogleDataConf;

	if (windowId && dlgConf)
		KWindowSystem::setMainWindow(dlgConf, windowId);

	dlgConf->exec();

	byteUser = dlgConf->eAccount->text().toLocal8Bit();
	bytePass = dlgConf->ePass->text().toLocal8Bit();
	user = const_cast<char *>(byteUser.constData());
	pass = const_cast<char *>(bytePass.constData());
	if (user)
		if (pass)
			result = gcal_get_authentication(gcal, user, pass);

	/* TODO: in case of authentication error, display an error
	 * message.
	 */
	if (!result)
		authenticated = true;

	synchronize();
}

void GoogleDataResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{

	Q_UNUSED(collection);

	KABC::Addressee addressee;
	gcal_contact_t contact;
	QString temp;
	int result;

	if (!authenticated) {
		kError() << "No athentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "No yet authenticated for"
					      " use of Google Contacts");
		emit error(message);

		emit status(Broken, message);
		return;
	}

	if (item.hasPayload<KABC::Addressee>())
		addressee = item.payload<KABC::Addressee>();

	if (!(contact = gcal_contact_new(NULL)))
		exit(1);

	temp = addressee.realName();
	gcal_contact_set_title(contact, const_cast<char *>(qPrintable(temp)));

	temp = addressee.fullEmail();
	gcal_contact_set_email(contact, const_cast<char *>(qPrintable(temp)));

	/* TODO: add remaining fields */

	if ((result = gcal_add_contact(gcal, contact)))
		exit(1);

	gcal_contact_delete(contact);

}

void GoogleDataResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED(parts);

	KABC::Addressee addressee;
	gcal_contact_t contact;
	KABC::Key key;
	QString temp;
	int result;

	if (!authenticated) {
		kError() << "No athentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "No yet authenticated for"
					      " use of Google Contacts");
		emit error(message);

		emit status(Broken, message);
		return;
	}

	if (item.hasPayload<KABC::Addressee>())
		addressee = item.payload<KABC::Addressee>();

	if (!(contact = gcal_contact_new(NULL)))
		exit(1);

	temp = addressee.realName();
	gcal_contact_set_title(contact, const_cast<char *>(qPrintable(temp)));

	temp = addressee.fullEmail();
	gcal_contact_set_email(contact, const_cast<char *>(qPrintable(temp)));

	temp = addressee.uid();
	gcal_contact_set_id(contact, const_cast<char *>(qPrintable(temp)));

	/* I suppose that this retrieves the first element in the key list */
	key = addressee.keys()[0];
	temp = key.id();
	gcal_contact_set_etag(contact, const_cast<char *>(qPrintable(temp)));


	/* TODO: add remaining fields */

	if ((result = gcal_update_contact(gcal, contact)))
		exit(1);


	/* Updates the ETag/url: I suppose that akonadi will save this object */
	temp = gcal_contact_get_url(contact);
	addressee.setUid(temp);

	temp = gcal_contact_get_etag(contact);
	key.setId(temp);
	addressee.insertKey(key);


	gcal_contact_delete(contact);

}

void GoogleDataResource::itemRemoved( const Akonadi::Item &item )
{
	KABC::Addressee addressee;
	gcal_contact_t contact;
	KABC::Key key;
	QString temp;
	int result;

	if (!authenticated) {
		kError() << "No athentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "No yet authenticated for"
					      " use of Google Contacts");
		emit error(message);

		emit status(Broken, message);
		return;
	}

	if (item.hasPayload<KABC::Addressee>())
		addressee = item.payload<KABC::Addressee>();

	if (!(contact = gcal_contact_new(NULL)))
		exit(1);

	temp = addressee.uid();
	gcal_contact_set_id(contact, const_cast<char *>(qPrintable(temp)));

	/* I suppose that this retrieves the first element in the key list */
	key = addressee.keys()[0];
	temp = key.id();
	gcal_contact_set_etag(contact, const_cast<char *>(qPrintable(temp)));

	if ((result = gcal_erase_contact(gcal, contact)))
		exit(1);

	gcal_contact_delete(contact);

}

AKONADI_RESOURCE_MAIN( GoogleDataResource )

#include "googledataresource.moc"
