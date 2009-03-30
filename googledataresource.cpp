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
 * - dialog displaying (kwallet + user account) is a bit confusing right
 * now, should display unlock dialog only if user got authenticated.
 * - support more than 1 user account
 * - retrieve KDE proxy and use it (KProtocolManager::proxyFor can help)
 * - test with special characters (unicode > 256)
 * - support google calendar (libgcal already has code for that)
 * - code cleanup
 * - unit tests: not sure if really required, libgcal already has lots
 * of tests
 * - nice to have: a libqcal (using Qt for both networking and XML/XPath parsing)
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

using KWallet::Wallet;

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

	wallet = 0;
}

GoogleDataResource::~GoogleDataResource()
{
	gcal_delete(gcal);
	gcal_cleanup_contacts(&all_contacts);
	if (dlgConf)
		delete dlgConf;

	pending.clear();
	deleted.clear();
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
	gcal_contact_t contact;
	QString timestamp;
	QByteArray t_byte;

	kError() << "\n............. retrieveItems ...........\n";
	if (!authenticated) {
		kError() << "No authentication for Google Contacts available";
		const QString message = i18nc("@info:status",
					      "Not yet authenticated for"
					      " use of Google Contacts");
		emit error(message);
		emit status(Broken, message);
		return;
	}

	/* Query by updated */
	retrieveTimestamp(timestamp);
	t_byte = timestamp.toLocal8Bit();
	if (t_byte.length() > TIMESTAMP_SIZE) {
		result = getUpdated(t_byte.data());
		return;
	}
	kError() << "First retrieve";

	/* Downloading the contacts can be slow and it is blocking. Will
	 * it mess up with akonadi?
	 */
	if ((result = gcal_get_contacts(gcal, &all_contacts)))
		exit(1);

	/* Contacts return last updated entry as last element */
	contact = gcal_contact_element(&all_contacts, all_contacts.length - 1);
	if (!contact) {
		kError() << "Failed to retrieve last updated contact.";
		const QString message = i18nc("@info:status",
					      "Failed getting last updated"
					      " contact");
		emit error(message);
		emit status(Broken, message);
		return;

	}

	timestamp = gcal_contact_get_updated(contact);
	saveTimestamp(timestamp);


	/* Each google entry has a unique ID and edit_url */
	for (size_t i = 0; i < all_contacts.length; ++i) {
		Item item(QLatin1String("text/directory"));
		contact = gcal_contact_element(&all_contacts, i);

		KABC::Addressee addressee;
		KABC::PhoneNumber number;
		KABC::Address address;
		QString temp;

		/* name */
		temp = gcal_contact_get_title(contact);
		addressee.setNameFromString(temp);
		/* email */
		temp = gcal_contact_get_email(contact);
		addressee.insertEmail(temp, true);
		/* address */
		temp = gcal_contact_get_address(contact);
		address.setExtended(temp);
		addressee.insertAddress(address);
		/* telephone */
		temp = gcal_contact_get_phone(contact);
		number.setNumber(temp);
		addressee.insertPhoneNumber(number);
		/* profission */
		temp = gcal_contact_get_profission(contact);
		addressee.setTitle(temp);
		/* company */
		temp = gcal_contact_get_organization(contact);
		addressee.setOrganization(temp);
		/* description */
		temp = gcal_contact_get_content(contact);
		addressee.setNote(temp);
		item.setPayload<KABC::Addressee>(addressee);

		/* remoteID: edit_url */
		KUrl urlEtag(gcal_contact_get_url(contact));
		item.setRemoteId(urlEtag.url());

		items << item;
	}

	itemsRetrieved(items);
}

bool GoogleDataResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED( parts );
	Q_UNUSED( item );
	return true;
}

void GoogleDataResource::aboutToQuit()
{
	// TODO: any cleanup you need to do while there is still an active
	// event loop. The resource will terminate after this method returns
}

int GoogleDataResource::saveToWallet(const QString &user, const QString &pass,
				     const WId &window, const QString &folder,
				     const QString &awallet)
{
	int result = -1;
	QString gaccount("googleAccount");
	if (wallet == 0)
		wallet = Wallet::openWallet(awallet, window);

	if (wallet == 0)
		return result;

	if (wallet->isOpen()) {
		if (!wallet->hasFolder(folder))
			wallet->createFolder(folder);
		wallet->setFolder(folder);
		QMap<QString, QString> data;
		data["login"] = user;
		data["password"] = pass;
		wallet->writeMap(gaccount, data);
		wallet->sync();
		result = 0;
	}

	return result;
}

int GoogleDataResource::retrieveFromWallet(QString &user,
					   QString &pass,
					   const WId &window,
					   const QString &folder,
					   const QString &awallet)
{
	int result = -1;
	QString gaccount("googleAccount");
	if (wallet == 0)
		wallet = Wallet::openWallet(awallet, window);

	if (wallet == 0)
		return result;

	if (wallet->isOpen()) {
		if (!wallet->hasFolder(folder))
			return result;
		wallet->setFolder(folder);
		QMap<QString, QString> data;
		if (!wallet->readMap(gaccount, data)) {
			user = data["login"];
			pass = data["password"];
			result = 0;
		}
	}

	return result;

}

void GoogleDataResource::doSetOnline(bool online)
{
	/* Approach based on kabcresource.cpp */
	kDebug() << "online" << online;
	QString user;
	QString password;
	int result = 0;
	WId window = winIdForDialogs();

	if (online)
		if (!retrieveFromWallet(user, password, window))
			if (!(result = authenticate(user, password))) {
				authenticated = true;
				ResourceBase::doSetOnline(online);
				synchronize();
			}

	if (result) {
		kError() << "Failed setting online.";
		const QString message = i18nc("@info:status",
					      "Invalid password.");
		emit error(message);
		emit status(Broken, message);
		return;
	}
}

void GoogleDataResource::retrieveTimestamp(QString &timestamp)
{
	timestamp = Settings::self()->timestamp();
}

void GoogleDataResource::saveTimestamp(QString &timestamp)
{
 	Settings::self()->setTimestamp(timestamp);
 	Settings::self()->writeConfig();
}

int GoogleDataResource::getUpdated(char *timestamp)
{
	int result = 1;
	gcal_contact_t contact;
	QString newerTimestamp;
	QString temp;

	/* Just in case, I'm not sure when this member function is called */
	pending.clear();
	deleted.clear();

	kError() << "Timestamp of last updated contact is: " << timestamp;
	gcal_cleanup_contacts(&all_contacts);
	gcal_deleted(gcal, SHOW);
	if ((result = gcal_get_updated_contacts(gcal, &all_contacts, timestamp))) {
		kError() << "Failed querying by updated";
		return result;
	}
	kError() << "Updated contacts are: " << all_contacts.length;


	/* Query is inclusive regarding timestamp */
	if (all_contacts.length == 1) {
		kError() << "no updates, done!";
		return result;
	}

	/* First element was already included in last retrieval, because
	 * query-by-updated is inclusive.
	 */
	for (size_t i = 0; i < all_contacts.length; ++i) {
		contact = gcal_contact_element(&all_contacts, i);
		Item item(QLatin1String("text/directory"));
		if (!strcmp(timestamp, gcal_contact_get_updated(contact))) {
			kError() << "This is an old contact... continue.";
			continue;
		}

		if (!gcal_contact_is_deleted(contact)) {
			KABC::Addressee addressee;
			KABC::PhoneNumber number;
			KABC::Address address;
			/* name */
			temp = gcal_contact_get_title(contact);
			addressee.setNameFromString(temp);
			kError() << "index: " << i <<"updated: " << temp;
			/* email */
			temp = gcal_contact_get_email(contact);
			addressee.insertEmail(temp, true);
			/* address */
			temp = gcal_contact_get_address(contact);
			address.setExtended(temp);
			addressee.insertAddress(address);
			/* telephone */
			temp = gcal_contact_get_phone(contact);
			number.setNumber(temp);
			addressee.insertPhoneNumber(number);
			/* profission */
			temp = gcal_contact_get_profission(contact);
			addressee.setTitle(temp);
			/* company */
			temp = gcal_contact_get_organization(contact);
			addressee.setOrganization(temp);
			/* description */
			temp = gcal_contact_get_content(contact);
			addressee.setNote(temp);
			item.setPayload<KABC::Addressee>(addressee);

			/* remoteID: edit_url */
			KUrl urlEtag(gcal_contact_get_url(contact));
			item.setRemoteId(urlEtag.url());
			pending << item;

		} else {

			KUrl urlEtag(gcal_contact_get_url(contact));
			item.setRemoteId(urlEtag.url());
			kError() << "index: " << i
				 << "deleted: " << urlEtag.url();
			deleted << item;
		}

	}

	itemsRetrievedIncremental(pending, deleted);

	/* Contacts return last updated entry as last element */
	contact = gcal_contact_element(&all_contacts,
				       all_contacts.length - 1);
	if (!contact) {
		kError() << "Failed to retrieve last updated contact.";
		const QString message = i18nc("@info:status",
					      "Failed getting last"
					      " updated"
					      " contact");
		emit error(message);
		emit status(Broken, message);
		result = -1;
		return result;

	}

	newerTimestamp = gcal_contact_get_updated(contact);
	saveTimestamp(newerTimestamp);

	return result;
}

int GoogleDataResource::authenticate(const QString &user,
				     const QString &password)
{

	QByteArray byteUser, bytePass;
	char *l_user, *l_pass;
	int result = -1;

	byteUser = user.toLocal8Bit();
	bytePass = password.toLocal8Bit();

	l_user = const_cast<char *>(byteUser.constData());
	l_pass = const_cast<char *>(bytePass.constData());

	if (l_user && l_pass)
		if (!(result = gcal_get_authentication(gcal, l_user, l_pass)))
			authenticated = true;

	return result;

}

void GoogleDataResource::configure( WId windowId )
{
	Q_UNUSED( windowId );
	int result = -1;

	if (!dlgConf)
		dlgConf = new dlgGoogleDataConf;

	if (windowId && dlgConf)
		KWindowSystem::setMainWindow(dlgConf, windowId);

	dlgConf->exec();
	/* TODO: in case of authentication error, display an error
	 * message.
	 */
	if (!(result = authenticate(dlgConf->eAccount->text(),
				    dlgConf->ePass->text()))) {
		result = saveToWallet(dlgConf->eAccount->text(),
				      dlgConf->ePass->text(),
				      windowId);

		synchronize();
	}

	if (result) {
		kError() << "Failed configuring resource.";
		const QString message = i18nc("@info:status",
					      "Invalid password.");
		emit error(message);
		emit status(Broken, message);
		return;
	}

}

void GoogleDataResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{

	Q_UNUSED(collection);

	KABC::Addressee addressee;
	KABC::PhoneNumber number;
	KABC::Address address;
	gcal_contact_t contact;
	QString temp;
	QByteArray t_byte;
	QList<KABC::Address> listAddress;
	QList<KABC::PhoneNumber> listNumber;
	KABC::Picture photo;
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

	/* This 2 fields are required! */
	temp = addressee.realName();
	t_byte = temp.toLocal8Bit();
	gcal_contact_set_title(contact, t_byte.data());

	temp = addressee.preferredEmail();
	t_byte = temp.toLocal8Bit();
	gcal_contact_set_email(contact, t_byte.data());

	/* Bellow are optional */
	listAddress = addressee.addresses();
	if (!listAddress.empty()) {
		address = listAddress.first();
		temp = address.extended();
		if (temp.length()) {
			t_byte = temp.toLocal8Bit();
			gcal_contact_set_address(contact, t_byte.data());
		}

	}

	listNumber = addressee.phoneNumbers();
	if (!listNumber.empty()) {
		number = listNumber.first();
		temp = number.number();
		if (temp.length()) {
			t_byte = temp.toLocal8Bit();
			gcal_contact_set_phone(contact, t_byte.data());
		}
	}

	temp = addressee.title();
	if (temp.length()) {
		t_byte = temp.toLocal8Bit();
		gcal_contact_set_profission(contact, t_byte.data());
	}

	temp = addressee.organization();
	if (temp.length()) {
		t_byte = temp.toLocal8Bit();
		gcal_contact_set_organization(contact, t_byte.data());
	}

	temp = addressee.note();
	if (temp.length()) {
		t_byte = temp.toLocal8Bit();
		gcal_contact_set_content(contact, t_byte.data());
	}

	photo = addressee.photo();
	if (!photo.isEmpty()) {
		QImage raw = photo.data();
		QByteArray ba;
		QBuffer buffer(&ba);
		buffer.open(QIODevice::WriteOnly);
		raw.save(&buffer, "PNG");
		gcal_contact_set_photo(contact,
				       ba.data(),
				       ba.size());
	}

	if ((result = gcal_add_contact(gcal, contact))) {
		kError() << "Failed adding new contact"
			 << "name: " << addressee.realName()
			 << "email: " << addressee.preferredEmail();
		const QString message = i18nc("@info:status",
					      "Failed adding new contact");
		emit error(message);
		emit status(Broken, message);

	}

	/* remoteID: edit_url */
	KUrl urlEtag(gcal_contact_get_url(contact));

	Item newItem(item);
	newItem.setPayload<KABC::Addressee>(addressee);
	newItem.setRemoteId(urlEtag.url());
	changeCommitted(newItem);


	/* cleanup */
	gcal_contact_delete(contact);

}

void GoogleDataResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED(parts);

	KABC::Addressee addressee;
	KABC::PhoneNumber number;
	KABC::Address address;
	QList<KABC::Address> listAddress;
	QList<KABC::PhoneNumber> listNumber;
	gcal_contact_t contact;
	QByteArray t_byte;
	QString temp;
	KABC::Picture photo;
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

	/* This 2 fields are required! */
	temp = addressee.realName();
	t_byte = temp.toLocal8Bit();
	gcal_contact_set_title(contact, t_byte.data());

	temp = addressee.preferredEmail();
	t_byte = temp.toLocal8Bit();
	gcal_contact_set_email(contact, t_byte.data());

	/* Bellow are optional */
	listAddress = addressee.addresses();
	if (!listAddress.empty()) {
		address = listAddress.first();
		temp = address.extended();
		if (temp.length()) {
			t_byte = temp.toLocal8Bit();
			gcal_contact_set_address(contact, t_byte.data());
		}
	}

	listNumber = addressee.phoneNumbers();
	if (!listNumber.empty()) {
		number = listNumber.first();
		temp = number.number();
		if (temp.length()) {
			t_byte = temp.toLocal8Bit();
			gcal_contact_set_phone(contact, t_byte.data());
		}

	}

	temp = addressee.title();
	if (temp.length()) {
		t_byte = temp.toLocal8Bit();
		gcal_contact_set_profission(contact, t_byte.data());
	}

	temp = addressee.organization();
	if (temp.length()) {
		t_byte = temp.toLocal8Bit();
		gcal_contact_set_organization(contact, t_byte.data());
	}

	temp = addressee.note();
	if (temp.length()) {
		t_byte = temp.toLocal8Bit();
		gcal_contact_set_content(contact, t_byte.data());
	}

	photo = addressee.photo();
	if (!photo.isEmpty()) {
		QImage raw = photo.data();
		QByteArray ba;
		QBuffer buffer(&ba);
		buffer.open(QIODevice::WriteOnly);
		raw.save(&buffer, "PNG");
		gcal_contact_set_photo(contact,
				       ba.data(),
				       ba.size());
	}


	KUrl url(item.remoteId());
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

	/* remoteID: edit_url */
	KUrl urlEtag(gcal_contact_get_url(contact));

	Item newItem(item);
	newItem.setPayload<KABC::Addressee>(addressee);
	newItem.setRemoteId(urlEtag.url());
	changeCommitted(newItem);

	gcal_contact_delete(contact);
}

void GoogleDataResource::itemRemoved( const Akonadi::Item &item )
{
	KABC::Addressee addressee;
	gcal_contact_t contact;
	QString temp;
	QByteArray t_byte;
	int result;

	kError() << "Deleting one item...";

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

	changeProcessed();
	kError() << "done deleting!!";
}

AKONADI_RESOURCE_MAIN( GoogleDataResource )

#include "googledataresource.moc"
