/*  Copyright (C) 2009  Adenilson Cavalcanti <savagobr@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; by version 2 of the License or (at your
 *  choice) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* TODO:
 * - inform to akonadi new edit_url/etag (after each operation they will
 * change).
 * - store user account details somewhere (config file?)
 * - delete/edit: test further, seems to fail from time to time (maybe related
 * with previous)
 * - support more fields: address, fax, photo, etc. This will require new
 * code in libgcal
 * - test with lots of contacts (blocking retrieve of contacts can mess with
 * akonadi)
 * - proxy support (already implemented in libgcal): show an advanced option
 * in configuration dialog so user can setup its proxy
 * - code cleanup
 * - unit tests: not sure if really required, libgcal already has lots
 * of tests
 */
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
#include <akonadi/changerecorder.h>
#include <akonadi/itemfetchscope.h>
#include <KUrl>

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

	changeRecorder()->itemFetchScope().fetchFullPayload();

	if (!(gcal = gcal_new(GCONTACT)))
		exit(1);
	gcal_set_store_xml(gcal, 1);
	all_contacts.length = 0;
	all_contacts.entries = NULL;
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
		kError() << "No authentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "Not yet authenticated for"
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
		kError() << "No authentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "Not yet authenticated for"
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
		QString temp;

		/* name */
		temp = gcal_contact_get_title(contact);
		addressee.setNameFromString(temp);
		/* email */
		temp = gcal_contact_get_email(contact);
		addressee.insertEmail(temp, true);
		/* TODO: telefone, address, etc */

		item.setPayload<KABC::Addressee>(addressee);

		/* remoteID: etag+edit_url */
		KUrl urlEtag(gcal_contact_get_url(contact));
		urlEtag.addQueryItem("etag", gcal_contact_get_etag(contact));

#endif

		item.setRemoteId(urlEtag.url());

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

	if (!authenticated) {
		kError() << "No authentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "Not yet authenticated for"
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
		/* FIXME: remoteID == edit_url + ETag */
		if (entry_id == gcal_contact_get_id(contact)) {
			/* name */
			temp = gcal_contact_get_title(contact);
			addressee.setNameFromString(temp);

			/* email */
			temp = gcal_contact_get_email(contact);
			addressee.insertEmail(temp, true);

			/* TODO: telefone, address, etc */

			/* remoteID: etag+edit_url */
			KUrl urlEtag(gcal_contact_get_url(contact));
			urlEtag.addQueryItem("etag",
					     gcal_contact_get_etag(contact));

			newItem.setPayload<KABC::Addressee>(addressee);
			newItem.setRemoteId(urlEtag.url());
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
	QByteArray t_byte;
	int result;

	if (!authenticated) {
		kError() << "No authentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "Not yet authenticated for"
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
	t_byte = temp.toLocal8Bit();
	gcal_contact_set_title(contact, t_byte.data());

	temp = addressee.fullEmail();
	t_byte = temp.toLocal8Bit();
	gcal_contact_set_email(contact, t_byte.data());

	/* TODO: add remaining fields */

	if ((result = gcal_add_contact(gcal, contact))) {
		kError() << "Failed adding new contact"
			 << "name: " << addressee.realName()
			 << "email: " << addressee.fullEmail();
		const QString message = i18nc("@info:status",
					      "Failed adding new contact");
		emit error(message);
		emit status(Broken, message);

	}

	/* remoteID: etag+edit_url */
	KUrl urlEtag(gcal_contact_get_url(contact));
	urlEtag.addQueryItem("etag", gcal_contact_get_etag(contact));

	Item newItem(item);
	newItem.setPayload<KABC::Addressee>(addressee);
	newItem.setRemoteId(urlEtag.url());
	itemRetrieved(newItem);


	/* cleanup */
	gcal_contact_delete(contact);

}

void GoogleDataResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED(parts);

	KABC::Addressee addressee;
	gcal_contact_t contact;
	QByteArray t_byte;
	QString temp;
	int result;

	if (!authenticated) {
		kError() << "No authentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "Not yet authenticated for"
					      " use of Google Contacts");
		emit error(message);

		emit status(Broken, message);
		return;
	}

	if (item.hasPayload<KABC::Addressee>())
		addressee = item.payload<KABC::Addressee>();

	if (!(contact = gcal_contact_new(NULL))) {
		kError() << "Memory allocation error!";
		const QString message = i18nc("@info:status",
					      "Failed to create gcal_contact");
		emit error(message);
		emit status(Broken, message);
		return;
	}

	temp = addressee.realName();
	t_byte = temp.toLocal8Bit();
	gcal_contact_set_title(contact, t_byte.data());

	temp = addressee.fullEmail();
	t_byte = temp.toLocal8Bit();
	gcal_contact_set_email(contact, t_byte.data());

	/* TODO: add remaining fields */

	KUrl url(item.remoteId());
	temp = url.queryItem("etag");
	t_byte = temp.toAscii();
	gcal_contact_set_etag(contact, t_byte.data());

	url.removeQueryItem("etag");
	temp = url.url();
	t_byte = temp.toAscii();
	gcal_contact_set_url(contact, t_byte.data());

	if ((result = gcal_update_contact(gcal, contact))) {
		kError() << "Failed editing contact";
		const QString message = i18nc("@info:status",
					      "Failed editing new contact");
		emit error(message);
		emit status(Broken, message);

	}

	/* remoteID: etag+edit_url */
	KUrl urlEtag(gcal_contact_get_url(contact));
	urlEtag.addQueryItem("etag", gcal_contact_get_etag(contact));

	Item newItem(item);
	newItem.setPayload<KABC::Addressee>(addressee);
	newItem.setRemoteId(urlEtag.url());
	itemRetrieved(newItem);

	gcal_contact_delete(contact);
}

void GoogleDataResource::itemRemoved( const Akonadi::Item &item )
{
	KABC::Addressee addressee;
	gcal_contact_t contact;
	QString temp;
	QByteArray t_byte;
	int result;

	if (!authenticated) {
		kError() << "No authentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "Not yet authenticated for"
					      " use of Google Contacts");
		emit error(message);
		emit status(Broken, message);
		return;
	}

	if (!(contact = gcal_contact_new(NULL))) {
		kError() << "Memory allocation error!";
		const QString message = i18nc("@info:status",
					      "Failed to create gcal_contact");
		emit error(message);
		emit status(Broken, message);
		return;
	}


	KUrl url(item.remoteId());
	temp = url.queryItem("etag");
	t_byte = temp.toAscii();
	gcal_contact_set_etag(contact, t_byte.data());

	url.removeQueryItem("etag");
	temp = url.url();
	t_byte = temp.toAscii();
	gcal_contact_set_url(contact, t_byte.data());

	if ((result = gcal_erase_contact(gcal, contact))) {
		kError() << "Failed deleting contact";
		const QString message = i18nc("@info:status",
					      "Failed deleting new contact");
		emit error(message);
		emit status(Broken, message);

	}

	gcal_contact_delete(contact);
}

AKONADI_RESOURCE_MAIN( GoogleDataResource )

#include "googledataresource.moc"
