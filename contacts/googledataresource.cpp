/***********************************************************************/
/* gcalresource.h 						       */
/* 								       */
/* Copyright (C) 2009  Adenilson Cavalcanti <savagobr@yahoo.com>       */
/* 								       */
/* This library is free software; you can redistribute it and/or       */
/* modify it under the terms of the GNU Lesser General Public	       */
/* License as published by the Free Software Foundation; either	       */
/* version 2.1 of the License, or (at your option) any later version.  */
/*   								       */
/* This library is distributed in the hope that it will be useful,     */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/* Lesser General Public License for more details.		       */
/*  								       */
/* You should have received a copy of the GNU Lesser General Public    */
/* License along with this library; if not, write to the Free Software */
/* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA       */
/* 02110-1301  USA						       */
/***********************************************************************/

/* TODO:
 * - dialog displaying (kwallet + user account) is a bit confusing right
 * now, should display unlock dialog only if user got authenticated.
 * - support more than 1 user account
 * - Some duplicated code must be moved to a common function (setting
 * KABC::Addressee data in gcal_contact_t).
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


using namespace Akonadi;

GoogleContactsResource::GoogleContactsResource( const QString &id )
	: ResourceBase(id)
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

GoogleContactsResource::~GoogleContactsResource()
{

	gcal_cleanup_contacts(&all_contacts);
	pending.clear();
	deleted.clear();
}

void GoogleContactsResource::retrieveTimestamp(QString &timestamp)
{
	timestamp = Settings::self()->timestamp();
}

void GoogleContactsResource::saveTimestamp(QString &timestamp)
{
 	Settings::self()->setTimestamp(timestamp);
 	Settings::self()->writeConfig();
}


void GoogleContactsResource::retrieveCollections()
{

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

int GoogleContactsResource::authenticationError(const char *msgError, int signal)
{
	int result = 0;
	QString qmsg(msgError);
	if (!authenticated) {
		kError() << qmsg;
		emit error(qmsg);
		emit status(signal, qmsg);
		result = -1;
	}

	return result;
}

KABC::PhoneNumber::Type googleLabelToAkonadiType(gcal_phone_type label) {
	switch ( label ) {
		case P_HOME:
			return KABC::PhoneNumber::Home;
			break;
		case P_WORK:
			return KABC::PhoneNumber::Work;
			break;
		case P_MOBILE:
			return KABC::PhoneNumber::Cell;
			break;
		case P_CAR:
			return KABC::PhoneNumber::Car;
			break;
		case P_ISDN:
			return KABC::PhoneNumber::Isdn;
			break;
		case P_PAGER:
			return KABC::PhoneNumber::Pager;
			break;
		case P_HOME_FAX:
			return KABC::PhoneNumber::Home | KABC::PhoneNumber::Fax;
			break;
		case P_WORK_FAX:
			return KABC::PhoneNumber::Work | KABC::PhoneNumber::Fax;
			break;
		default:
			return KABC::PhoneNumber::Home | KABC::PhoneNumber::Work; // other
	}
}

void GoogleContactsResource::retrieveItems( const Akonadi::Collection &collection )
{
	Q_UNUSED( collection );

	Item::List items;
	int result;
	gcal_contact_t contact;
	QString timestamp;
	QByteArray t_byte;

	if (!authenticated)
		configure(0);

	/* Query by updated */
	retrieveTimestamp(timestamp);
	t_byte = timestamp.toUtf8();
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
					      " contact.");
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
		KABC::Address address;
		KABC::Picture photo;
		QImage image;
		QString temp;
		int j;

		/* name */
		temp = QString::fromUtf8(gcal_contact_get_title(contact));
		addressee.setNameFromString(temp);
		/* email */
		for (j = 0; j < gcal_contact_get_emails_count(contact); j++) {
			temp = QString::fromUtf8(gcal_contact_get_email_address(contact, j));
			addressee.insertEmail(temp, (j == gcal_contact_get_pref_email(contact)));
			temp = QString::number(gcal_contact_get_email_address_type(contact, j));
			addressee.insertCustom(QString::fromUtf8("Google"),
					QString::fromUtf8("typeof_email_").append(QString::number(j)), temp);
		}
		/* address */
		temp = QString::fromUtf8(gcal_contact_get_address(contact));
		address.setExtended(temp);
		addressee.insertAddress(address);
		/* telephone */
		for (j = 0; j < gcal_contact_get_phone_numbers_count(contact); j++) {
			KABC::PhoneNumber number;
			temp = QString::fromUtf8(gcal_contact_get_phone_number(contact, j));
			number.setNumber(temp);
			number.setType(googleLabelToAkonadiType(gcal_contact_get_phone_number_type(contact, j)));
			addressee.insertPhoneNumber(number);
		}
		/* profission */
		temp = QString::fromUtf8(gcal_contact_get_profission(contact));
		addressee.setTitle(temp);
		/* company */
		temp = QString::fromUtf8(gcal_contact_get_organization(contact));
		addressee.setOrganization(temp);
		/* Google group membership */
		addressee.insertCustom(QString::fromUtf8("Google"),
					QString::fromUtf8("groupMembership_nr"),
					QString::number(gcal_contact_get_groupMembership_count(contact)));

		for (j = 0; j < gcal_contact_get_groupMembership_count(contact); j++) {
			temp = QString::fromUtf8(gcal_contact_get_groupMembership(contact, j));
			addressee.insertCustom(QString::fromUtf8("Google"), QString::fromUtf8("groupMembership_").append(QString::number(j)), temp);
		}
		/* description */
		temp = QString::fromUtf8(gcal_contact_get_content(contact));
		addressee.setNote(temp);
		/* photo */
		if (gcal_contact_get_photolength(contact)) {
			QByteArray ba(gcal_contact_get_photo(contact),
				      (int)gcal_contact_get_photolength(contact));
			image.loadFromData(ba);
			photo.setData(image);
			addressee.setPhoto(photo);

		}

		item.setPayload<KABC::Addressee>(addressee);

		/* remoteID: edit_url */
		KUrl urlEtag(gcal_contact_get_url(contact));
		item.setRemoteId(urlEtag.url());

		items << item;
	}

	itemsRetrieved(items);
	kError() << "\n............. done retrieveItems! ...........\n";
}

bool GoogleContactsResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED( parts );
	Q_UNUSED( item );
	return true;
}

void GoogleContactsResource::aboutToQuit()
{
	// TODO: any cleanup you need to do while there is still an active
	// event loop. The resource will terminate after this method returns
}

void GoogleContactsResource::doSetOnline(bool online)
{
	/* Approach based on kabcresource.cpp */
	kDebug() << "online: " << online;
	QString user;
	QString password;
	int result = 0;
	WId window = winIdForDialogs();

	if (online)
		if (!retrieveFromWallet(user, password, window))
			if (!(result = authenticate(user, password))) {
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

int GoogleContactsResource::getUpdated(char *timestamp)
{
	int result = 1;
	gcal_contact_t contact;
	QString newerTimestamp;
	QString temp;
	KABC::Picture photo;
	QImage image;
	int j;

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
	/* RFC: don't think so... */
	if (all_contacts.length == 0) {
		kError() << "no updates, done!";
		itemsRetrievedIncremental(pending, deleted);
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
			KABC::Address address;
			/* name */
			temp = QString::fromUtf8(gcal_contact_get_title(contact));
			addressee.setNameFromString(temp);
			kError() << "index: " << i <<"updated: " << temp;
			/* email */
			for (j = 0; j < gcal_contact_get_emails_count(contact); j++) {
				temp = QString::fromUtf8(gcal_contact_get_email_address(contact, j));
				addressee.insertEmail(temp, (j == gcal_contact_get_pref_email(contact)));
				temp = QString::number(gcal_contact_get_email_address_type(contact, j));
				addressee.insertCustom(QString::fromUtf8("Google"),
						QString::fromUtf8("typeof_email_").append(QString::number(j)), temp);
			}
			/* address */
			temp = QString::fromUtf8(gcal_contact_get_address(contact));
			address.setExtended(temp);
			addressee.insertAddress(address);
			/* telephone */
			for (j = 0; j < gcal_contact_get_phone_numbers_count(contact); j++) {
				KABC::PhoneNumber number;
				temp = QString::fromUtf8(gcal_contact_get_phone_number(contact, j));
				number.setNumber(temp);
				number.setType(googleLabelToAkonadiType(gcal_contact_get_phone_number_type(contact, j)));
				addressee.insertPhoneNumber(number);
			}
			/* profission */
			temp = QString::fromUtf8(gcal_contact_get_profission(contact));
			addressee.setTitle(temp);
			/* company */
			temp = QString::fromUtf8(gcal_contact_get_organization(contact));
			addressee.setOrganization(temp);
			/* Google group membership */
			addressee.insertCustom(QString::fromUtf8("Google"),
						QString::fromUtf8("groupMembership_nr"),
						QString::number(gcal_contact_get_groupMembership_count(contact)));

			for (j = 0; j < gcal_contact_get_groupMembership_count(contact); j++) {
				temp = QString::fromUtf8(gcal_contact_get_groupMembership(contact, j));
				addressee.insertCustom(QString::fromUtf8("Google"), QString::fromUtf8("groupMembership_").append(QString::number(j)), temp);
			}
			/* description */
			temp = QString::fromUtf8(gcal_contact_get_content(contact));
			addressee.setNote(temp);
			/* photo */
			if (gcal_contact_get_photolength(contact)) {
				QByteArray ba(gcal_contact_get_photo(contact),
					      (int)gcal_contact_get_photolength(contact));
				image.loadFromData(ba);
				photo.setData(image);
				addressee.setPhoto(photo);

			}

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
					      " contact.");
		emit error(message);
		emit status(Broken, message);
		result = -1;
		return result;

	}

	newerTimestamp = gcal_contact_get_updated(contact);
	saveTimestamp(newerTimestamp);

	return result;
}

void GoogleContactsResource::configure( WId windowId )
{
	if (windowId && dlgConf)
		KWindowSystem::setMainWindow(dlgConf, windowId);

	dlgConf->exec();
	int authRes = authenticate(dlgConf->eAccount->text(),
				   dlgConf->ePass->text());
	if (authRes) {
		kError() << "Failed configuring resource: Invalid password.";
		const QString message = i18nc("@info:status",
					      "Invalid password.");
		emit error(message);
		emit status(Broken, message);
		return;
	}

	int walletRes = saveToWallet(dlgConf->eAccount->text(),
				     dlgConf->ePass->text(),
				     windowId);
	if (walletRes)
		kError() << "Cannot save user info: is user using kwallet?.";


	synchronize();
}

gcal_phone_type akonadiTypeToGoogleLabel(KABC::PhoneNumber::Type type) {
	switch ( type ) {
		case KABC::PhoneNumber::Home:
			return P_HOME;
			break;
		case KABC::PhoneNumber::Work:
			return P_WORK;
			break;
		case KABC::PhoneNumber::Cell:
			return P_MOBILE;
			break;
		case KABC::PhoneNumber::Car:
			return P_CAR;
			break;
		case KABC::PhoneNumber::Isdn:
			return P_ISDN;
			break;
		case KABC::PhoneNumber::Pager:
			return P_PAGER;
			break;
		case KABC::PhoneNumber::Home + KABC::PhoneNumber::Fax:
			return P_HOME_FAX;
			break;
		case KABC::PhoneNumber::Work + KABC::PhoneNumber::Fax:
			return P_WORK_FAX;
			break;
		default:
			return P_OTHER;
	}
}

void GoogleContactsResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{

	Q_UNUSED(collection);

	KABC::Addressee addressee;
	KABC::Address address;
	gcal_contact_t contact;
	QString temp;
	QStringList listEmail;
	QStringList::const_iterator email;
	QByteArray t_byte;
	QList<KABC::Address> listAddress;
	QList<KABC::PhoneNumber> listNumber;
	KABC::Picture photo;
	int result;
	int num_elem;
	int j;
	bool ok;

	if (!authenticated)
		configure(0);
	if (!authenticated) {
		authenticationError("itemAdded: not authenticated!", Broken);
		return;
	}

	if (item.hasPayload<KABC::Addressee>()) {
		addressee = item.payload<KABC::Addressee>();
		if (addressee.isEmpty()) {
			kError() << "itemAdded: Null contact! Returning...";
			return;
		}
	}

	if (!(contact = gcal_contact_new(NULL)))
		exit(1);

	/* This 2 fields are required! */
	temp = addressee.realName();
	t_byte = temp.toUtf8();
	gcal_contact_set_title(contact, t_byte.data());

	listEmail = addressee.emails();
	if (!listEmail.empty()) {
		gcal_contact_delete_email_addresses(contact);
		for (email = listEmail.constBegin(); email != listEmail.constEnd(); email++) {
			if (email->length()) {
				t_byte = email->toUtf8();

				temp = addressee.custom(QString::fromUtf8("Google"),
						QString::fromUtf8("typeof_email_").append(QString::number(email - listEmail.constBegin())));
				j = temp.toInt(&ok);

				gcal_contact_add_email_address(contact, t_byte.data(), (ok ? (gcal_email_type)j : E_OTHER),
							(*email == addressee.preferredEmail()));
			}
		}
	}

	/* Bellow are optional */
	listAddress = addressee.addresses();
	if (!listAddress.empty()) {
		address = listAddress.first();
		temp = address.extended();
		if (temp.length()) {
			t_byte = temp.toUtf8();
			gcal_contact_set_address(contact, t_byte.data());
		}
	}

	listNumber = addressee.phoneNumbers();
	if (!listNumber.empty()) {
		gcal_contact_delete_phone_numbers(contact);
		foreach (const KABC::PhoneNumber &number, listNumber) {
			temp = number.number();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_add_phone_number(contact, t_byte.data(),
							akonadiTypeToGoogleLabel(number.type()));
			}
		}
	}

	temp = addressee.title();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_profission(contact, t_byte.data());
	}

	temp = addressee.organization();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_organization(contact, t_byte.data());
	}

	temp = addressee.custom(QString::fromUtf8("Google"), QString::fromUtf8("groupMembership_nr"));
	num_elem = temp.toInt(&ok);
	if (ok) {
		gcal_contact_delete_groupMembership(contact);
		for (j = 0; j < num_elem; j++) {
			temp = addressee.custom(QString::fromUtf8("Google"), QString::fromUtf8("groupMembership_").append(QString::number(j)));
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_add_groupMembership(contact, t_byte.data());
			}
		}
	}

	temp = addressee.note();
	if (temp.length()) {
		t_byte = temp.toUtf8();
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
					      "Failed adding new contact.");
		emit error(message);
		emit status(Broken, message);

	}

	/* remoteID: edit_url */
	KUrl urlEtag(gcal_contact_get_url(contact));

	Item newItem(item);
	/* TODO: what about new updated field? */
	newItem.setPayload<KABC::Addressee>(addressee);
	newItem.setRemoteId(urlEtag.url());
	changeCommitted(newItem);


	/* cleanup */
	gcal_contact_delete(contact);

}

void GoogleContactsResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED(parts);

	KABC::Addressee addressee;
	KABC::Address address;
	QList<KABC::Address> listAddress;
	QStringList listEmail;
	QStringList::const_iterator email;
	QList<KABC::PhoneNumber> listNumber;
	gcal_contact_t contact;
	QByteArray t_byte;
	QString temp;
	KABC::Picture photo;
	int result;
	int num_elem;
	int j;
	bool ok;

	if (!authenticated)
		configure(0);
	if (!authenticated) {
		authenticationError("itemChanged: not authenticated!",
				    Broken);
		return;
	}

	if (item.hasPayload<KABC::Addressee>()) {
		addressee = item.payload<KABC::Addressee>();
		if (addressee.isEmpty()) {
			kError() << "itemAdded: Null contact! Returning...";
			return;
		}
	}

	if (!(contact = gcal_contact_new(NULL))) {
		kError() << "Memory allocation error!";
		const QString message = i18nc("@info:status",
					      "Failed to create gcal_contact.");
		emit error(message);
		emit status(Broken, message);
		return;
	}

	/* This 2 fields are required! */
	temp = addressee.realName();
	t_byte = temp.toUtf8();
	gcal_contact_set_title(contact, t_byte.data());

	listEmail = addressee.emails();
	if (!listEmail.empty()) {
		gcal_contact_delete_email_addresses(contact);
		for (email = listEmail.constBegin(); email != listEmail.constEnd(); email++) {
			if (email->length()) {
				t_byte = email->toUtf8();

				temp = addressee.custom(QString::fromUtf8("Google"),
						QString::fromUtf8("typeof_email_").append(QString::number(email - listEmail.constBegin())));
				j = temp.toInt(&ok);

				gcal_contact_add_email_address(contact, t_byte.data(), (ok ? (gcal_email_type)j : E_OTHER),
							(*email == addressee.preferredEmail()));
			}
		}
	}

	/* Bellow are optional */
	listAddress = addressee.addresses();
	if (!listAddress.empty()) {
		address = listAddress.first();
		temp = address.extended();
		if (temp.length()) {
			t_byte = temp.toUtf8();
			gcal_contact_set_address(contact, t_byte.data());
		}
	}

	listNumber = addressee.phoneNumbers();
	if (!listNumber.empty()) {
		gcal_contact_delete_phone_numbers(contact);
		foreach (const KABC::PhoneNumber &number, listNumber) {
			temp = number.number();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_add_phone_number(contact, t_byte.data(),
							akonadiTypeToGoogleLabel(number.type()));
			}
		}
	}

	temp = addressee.title();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_profission(contact, t_byte.data());
	}

	temp = addressee.organization();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_organization(contact, t_byte.data());
	}

	temp = addressee.custom(QString::fromUtf8("Google"), QString::fromUtf8("groupMembership_nr"));
	num_elem = temp.toInt(&ok);
	if (ok) {
		gcal_contact_delete_groupMembership(contact);
		for (j = 0; j < num_elem; j++) {
			temp = addressee.custom(QString::fromUtf8("Google"), QString::fromUtf8("groupMembership_").append(QString::number(j)));
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_add_groupMembership(contact, t_byte.data());
			}
		}
	}

	temp = addressee.note();
	if (temp.length()) {
		t_byte = temp.toUtf8();
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
					      "Failed editing new contact.");
		emit error(message);
		emit status(Broken, message);

	}

	/* remoteID: edit_url */
	temp = gcal_contact_get_url(contact);
	url = temp;

	Item newItem(item);
	newItem.setPayload<KABC::Addressee>(addressee);
	newItem.setRemoteId(url.url());
	changeCommitted(newItem);

	gcal_contact_delete(contact);
}

void GoogleContactsResource::itemRemoved( const Akonadi::Item &item )
{
	KABC::Addressee addressee;
	gcal_contact_t contact;
	QString temp;
	QByteArray t_byte;
	int result;

	kError() << "Deleting one item...";

	if (!authenticated)
		configure(0);
	if (!authenticated) {
		authenticationError("itemRemoved: not authenticated!",
				    Broken);
		return;
	}

	if (!(contact = gcal_contact_new(NULL))) {
		kError() << "Memory allocation error!";
		const QString message = i18nc("@info:status",
					      "Failed to create gcal_contact.");
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
					      "Failed deleting new contact.");
		emit error(message);
		emit status(Broken, message);

	}

	gcal_contact_delete(contact);

	changeProcessed();
	kError() << "done deleting!!";
}

AKONADI_RESOURCE_MAIN( GoogleContactsResource )

#include "googledataresource.moc"
