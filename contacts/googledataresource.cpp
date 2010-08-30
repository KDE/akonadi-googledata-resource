/***********************************************************************/
/* gcalresource.h 						       */
/* 								       */
/* Copyright (C) 2009  Adenilson Cavalcanti <savagobr@yahoo.com>       */
/* Copyright (C) 2010  Holger Kral <holger.kral@gmx.net>               */
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
 * - code cleanup: remove multiple calls to authenticate()
 * - dialog displaying (kwallet + user account) is a bit confusing right
 * now, should display unlock dialog only if user got authenticated.
 * - support more than 1 user account
 * - Some duplicated code must be moved to a common function (setting
 * KABC::Addressee data in gcal_contact_t).
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
#include <libgcal/gcalendar.h>
#include <libgcal/gcontact.h>
#include <libgcal/gcal_status.h>
}


using namespace Akonadi;

QString GoogleContactsResource::extractStructuredField(gcal_structured_subvalues_t structured_entry, char *fieldName, int index1, int index2)
{
    QString res;
    res = QString::fromUtf8(gcal_contact_get_structured_entry(structured_entry, index1, index2, fieldName));
    return res;
}

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

        setNeedsNetwork(true);
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

KABC::PhoneNumber::Type googlePhoneLabelToAkonadiType(gcal_phone_type label) {
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

KABC::Address::Type googleAddressLabelToAkonadiType(gcal_address_type label) {
	switch (label) {
		case A_HOME:
			return KABC::Address::Home;
			break;
		case A_WORK:
			return KABC::Address::Work;
			break;
		case A_OTHER:
			return KABC::Address::Postal;
			break;
		default:
			return KABC::Address::Home; // other
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
	if (!authenticated) {
		ResourceBase::cancelTask(QString("Failed retrieving contacts!"));
		ResourceBase::doSetOnline(false);
		emit error(QString("retrieveItems: not authenticated!"));
		return;
	}

	/* Query by updated */
	retrieveTimestamp(timestamp);
	t_byte = timestamp.toUtf8();
	if (t_byte.length() > TIMESTAMP_SIZE) {
		result = getUpdated(t_byte.data());
		return;
	}
	kError() << "First retrieve";

	/* Downloading the contacts can be slow and it is blocking.
	 */
	if ((result = gcal_get_contacts(gcal, &all_contacts))) {
		ResourceBase::cancelTask(QString("Failed contacts retrieving!"));
		ResourceBase::doSetOnline(false);
		return;
        }

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


        QStringList listImTypes;
        listImTypes << "AIM" << "MSN" << "YAHOO" << "SKYPE" << "ICQ" << "JABBER" << "QQ";
	/* Each google entry has a unique ID and edit_url */
	for (size_t i = 0; i < all_contacts.length; ++i) {
		Item item(QLatin1String("text/directory"));
		contact = gcal_contact_element(&all_contacts, i);

		KABC::Addressee addressee;
		KABC::Picture photo;
		KABC::Address::Type temp_address_type;
		QImage image;
		QString temp;
		QDateTime tempDate;
		KUrl tempUrl;
		gcal_structured_subvalues_t structured_entry;
		int structured_entry_count;
		int j;
		bool fill_entry;

		/* structured name */
		structured_entry = gcal_contact_get_structured_name(contact);
		fill_entry = 1;
		temp = extractStructuredField(structured_entry, "givenName");
		addressee.setGivenName(temp);
		if (temp.length())
			fill_entry = 0;

		temp = extractStructuredField(structured_entry, "additionalName");
		addressee.setAdditionalName(temp);
		if (temp.length())
			fill_entry = 0;

		temp = extractStructuredField(structured_entry, "familyName");
		addressee.setFamilyName(temp);
		if (temp.length())
			fill_entry = 0;

		temp = extractStructuredField(structured_entry, "namePrefix");
		addressee.setPrefix(temp);
		if (temp.length())
			fill_entry = 0;

		temp = extractStructuredField(structured_entry, "nameSuffix");
		addressee.setSuffix(temp);
		if (temp.length())
			fill_entry = 0;

		if (fill_entry) {
			temp = extractStructuredField(structured_entry, "fullName");
			addressee.setNameFromString(temp);
			if (temp.length())
				fill_entry = 0;
		}

		if (fill_entry) {
			/* name */
			temp = QString::fromUtf8(gcal_contact_get_title(contact));
			addressee.setNameFromString(temp);
		}

		/* nickname */
		temp = QString::fromUtf8(gcal_contact_get_nickname(contact));
		addressee.setNickName(temp);

		/* email */
		for (j = 0; j < gcal_contact_get_emails_count(contact); j++) {
			temp = QString::fromUtf8(gcal_contact_get_email_address(contact, j));
			addressee.insertEmail(temp, (j == gcal_contact_get_pref_email(contact)));
			temp = QString::number(gcal_contact_get_email_address_type(contact, j));
			addressee.insertCustom(QString::fromUtf8("Google"),
				QString::fromUtf8("typeof_email_").append(QString::number(j)), temp);
		}

		/* address */
		structured_entry = gcal_contact_get_structured_address(contact);
		structured_entry_count = gcal_contact_get_structured_address_count(contact);
		for (j = 0; j < structured_entry_count; j++) {
			KABC::Address address;
			fill_entry = 1;
			temp_address_type = googleAddressLabelToAkonadiType(gcal_contact_get_structured_address_type(contact, j, structured_entry_count));
			if (j == gcal_contact_get_pref_structured_address(contact))
				temp_address_type = temp_address_type | KABC::Address::Pref;
			address.setType(temp_address_type);

                        temp = extractStructuredField(structured_entry, "street", j, structured_entry_count);
			address.setStreet(temp);
			if (temp.length())
				fill_entry = 0;

                        temp = extractStructuredField(structured_entry, "pobox", j, structured_entry_count);
			address.setPostOfficeBox(temp);
			if (temp.length())
				fill_entry = 0;

                        temp = extractStructuredField(structured_entry, "city", j, structured_entry_count);
			address.setLocality(temp);
			if (temp.length())
				fill_entry = 0;

                        temp = extractStructuredField(structured_entry, "region", j, structured_entry_count);
			address.setRegion(temp);
			if (temp.length())
				fill_entry = 0;

                        temp = extractStructuredField(structured_entry, "postcode", j, structured_entry_count);
			address.setPostalCode(temp);
			if (temp.length())
				fill_entry = 0;

                        temp = extractStructuredField(structured_entry, "country", j, structured_entry_count);
			address.setCountry(temp);
			if (temp.length())
				fill_entry = 0;

			if (fill_entry) {
				temp = extractStructuredField(structured_entry, "formattedAddress", j, structured_entry_count);
				address.setStreet(temp);
			}
			addressee.insertAddress(address);
		}

		/* telephone */
		for (j = 0; j < gcal_contact_get_phone_numbers_count(contact); j++) {
			KABC::PhoneNumber number;
			temp = QString::fromUtf8(gcal_contact_get_phone_number(contact, j));
			number.setNumber(temp);
			number.setType(googlePhoneLabelToAkonadiType(gcal_contact_get_phone_number_type(contact, j)));
			addressee.insertPhoneNumber(number);
		}
		/* im */
		for (j = 0; j < gcal_contact_get_im_count(contact); j++) {
			temp = QString::fromUtf8(gcal_contact_get_im_protocol(contact, j));
			if (listImTypes.contains(temp)) {
				if ( temp == QString::fromUtf8("JABBER") )
					temp = QString::fromUtf8("xmpp");
				addressee.insertCustom(QString::fromUtf8("messaging/").append(temp.toLower()),
						QString::fromUtf8("All"),
						QString::fromUtf8(gcal_contact_get_im_address(contact, j)));
			}
		}
		/* profission */
		temp = QString::fromUtf8(gcal_contact_get_profission(contact));
		addressee.setTitle(temp);
		/* company */
		temp = QString::fromUtf8(gcal_contact_get_organization(contact));
		addressee.setOrganization(temp);
		/* occupation/profession */
		temp = QString::fromUtf8(gcal_contact_get_occupation(contact));
		addressee.insertCustom("KADDRESSBOOK","X-Profession",temp);
		/* Google group membership */
		addressee.insertCustom(QString::fromUtf8("Google"),
					QString::fromUtf8("groupMembership_nr"),
					QString::number(gcal_contact_get_groupMembership_count(contact)));

		for (j = 0; j < gcal_contact_get_groupMembership_count(contact); j++) {
			temp = QString::fromUtf8(gcal_contact_get_groupMembership(contact, j));
			addressee.insertCustom(QString::fromUtf8("Google"), QString::fromUtf8("groupMembership_").append(QString::number(j)), temp);
		}
		/* note */
		temp = QString::fromUtf8(gcal_contact_get_content(contact));
		addressee.setNote(temp);
		/* url */
		tempUrl = KUrl::fromPathOrUrl(QString::fromUtf8(gcal_contact_get_homepage(contact)));
		addressee.setUrl(tempUrl);
		/* blog */
		tempUrl = KUrl::fromPathOrUrl(QString::fromUtf8(gcal_contact_get_blog(contact)));
		addressee.insertCustom("KADDRESSBOOK","BlogFeed",tempUrl.prettyUrl());
		/* birthday */
		tempDate = QDateTime::fromString(gcal_contact_get_birthday(contact),"yyyy-MM-dd");
		if (!tempDate.isValid())
			tempDate = QDateTime::fromString(gcal_contact_get_birthday(contact),"--MM-dd");
		addressee.setBirthday(tempDate);
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
    gcal_final_cleanup();
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
					      "Invalid password or network proxy.");
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
	QStringList listImTypes;
	listImTypes << "AIM" << "MSN" << "YAHOO" << "SKYPE" << "ICQ" << "JABBER" << "QQ";
	QDateTime tempDate;
	KUrl tempUrl;
	KABC::Picture photo;
	KABC::Address::Type temp_address_type;
	QImage image;
	gcal_structured_subvalues_t structured_entry;
	int structured_entry_count;
	int j;
	bool fill_entry;

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
			/* structured name */
			structured_entry = gcal_contact_get_structured_name(contact);
			fill_entry = 1;
                        temp = extractStructuredField(structured_entry, "givenName");
			addressee.setGivenName(temp);
			if (temp.length())
				fill_entry = 0;

                        temp = extractStructuredField(structured_entry, "additionalName");
			addressee.setAdditionalName(temp);
			if (temp.length())
				fill_entry = 0;

                        temp = extractStructuredField(structured_entry, "familyName");
			addressee.setFamilyName(temp);
			if (temp.length())
				fill_entry = 0;


                        temp = extractStructuredField(structured_entry, "namePrefix");
			addressee.setPrefix(temp);
			if (temp.length())
				fill_entry = 0;


                        temp = extractStructuredField(structured_entry, "nameSuffix");
			addressee.setSuffix(temp);
			if (temp.length())
				fill_entry = 0;
			if (fill_entry) {

				temp = extractStructuredField(structured_entry, "fullName");
				addressee.setNameFromString(temp);
				if (temp.length())
					fill_entry = 0;
			}

			if (fill_entry) {
				/* name */
				temp = QString::fromUtf8(gcal_contact_get_title(contact));
				addressee.setNameFromString(temp);
				kError() << "index: " << i <<"updated: " << temp;
			}
			/* nickname */
			temp = QString::fromUtf8(gcal_contact_get_nickname(contact));
			addressee.setNickName(temp);
			/* email */
			for (j = 0; j < gcal_contact_get_emails_count(contact); j++) {
				temp = QString::fromUtf8(gcal_contact_get_email_address(contact, j));
				addressee.insertEmail(temp, (j == gcal_contact_get_pref_email(contact)));
				temp = QString::number(gcal_contact_get_email_address_type(contact, j));
				addressee.insertCustom(QString::fromUtf8("Google"),
						QString::fromUtf8("typeof_email_").append(QString::number(j)), temp);
			}

			/* address */
			structured_entry = gcal_contact_get_structured_address(contact);
			structured_entry_count = gcal_contact_get_structured_address_count(contact);
			for (j = 0; j < structured_entry_count; j++) {
				KABC::Address address;
				fill_entry = 1;
				temp_address_type = googleAddressLabelToAkonadiType(gcal_contact_get_structured_address_type(contact, j, structured_entry_count));
				if (j == gcal_contact_get_pref_structured_address(contact))
					temp_address_type = temp_address_type | KABC::Address::Pref;
				address.setType(temp_address_type);

                                temp = extractStructuredField(structured_entry, "street", j, structured_entry_count);
				address.setStreet(temp);
				if (temp.length())
					fill_entry = 0;

                                temp = extractStructuredField(structured_entry, "pobox", j, structured_entry_count);
				address.setPostOfficeBox(temp);
				if (temp.length())
					fill_entry = 0;

                                temp = extractStructuredField(structured_entry, "city", j, structured_entry_count);
				address.setLocality(temp);
				if (temp.length())
					fill_entry = 0;

                                temp = extractStructuredField(structured_entry, "region", j, structured_entry_count);
				address.setRegion(temp);
				if (temp.length())
					fill_entry = 0;

                                temp = extractStructuredField(structured_entry, "postcode", j, structured_entry_count);
				address.setPostalCode(temp);
				if (temp.length())
					fill_entry = 0;

                                temp = extractStructuredField(structured_entry, "country", j, structured_entry_count);
				address.setCountry(temp);
				if (temp.length())
					fill_entry = 0;
				if (fill_entry) {
                                        temp = extractStructuredField(structured_entry, "formattedAddress", j, structured_entry_count);
					address.setStreet(temp);
				}
				addressee.insertAddress(address);
			}

			/* telephone */
			for (j = 0; j < gcal_contact_get_phone_numbers_count(contact); j++) {
				KABC::PhoneNumber number;
				temp = QString::fromUtf8(gcal_contact_get_phone_number(contact, j));
				number.setNumber(temp);
				number.setType(googlePhoneLabelToAkonadiType(gcal_contact_get_phone_number_type(contact, j)));
				addressee.insertPhoneNumber(number);
			}
			/* im */
			for (j = 0; j < gcal_contact_get_im_count(contact); j++) {
				temp = QString::fromUtf8(gcal_contact_get_im_protocol(contact, j));
				if (listImTypes.contains(temp)) {
					if ( temp == QString::fromUtf8("JABBER") )
						temp = QString::fromUtf8("xmpp");

					addressee.insertCustom(QString::fromUtf8("messaging/").append(temp.toLower()),
							QString::fromUtf8("All"),
							QString::fromUtf8(gcal_contact_get_im_address(contact, j)));
				}
			}

			/* profission */
			temp = QString::fromUtf8(gcal_contact_get_profission(contact));
			addressee.setTitle(temp);
			/* company */
			temp = QString::fromUtf8(gcal_contact_get_organization(contact));
			addressee.setOrganization(temp);
			/* occupation/profession */
			temp = QString::fromUtf8(gcal_contact_get_occupation(contact));
			addressee.insertCustom("KADDRESSBOOK","X-Profession",temp);
			/* Google group membership */
			addressee.insertCustom(QString::fromUtf8("Google"),
						QString::fromUtf8("groupMembership_nr"),
						QString::number(gcal_contact_get_groupMembership_count(contact)));

			for (j = 0; j < gcal_contact_get_groupMembership_count(contact); j++) {
				temp = QString::fromUtf8(gcal_contact_get_groupMembership(contact, j));
				addressee.insertCustom(QString::fromUtf8("Google"), QString::fromUtf8("groupMembership_").append(QString::number(j)), temp);
			}

			/* note */
			temp = QString::fromUtf8(gcal_contact_get_content(contact));
			addressee.setNote(temp);
			/* url */
			tempUrl = KUrl::fromPathOrUrl(QString::fromUtf8(gcal_contact_get_homepage(contact)));
			addressee.setUrl(tempUrl);
			/* blog */
			tempUrl = KUrl::fromPathOrUrl(QString::fromUtf8(gcal_contact_get_blog(contact)));
			addressee.insertCustom("KADDRESSBOOK","BlogFeed",tempUrl.prettyUrl());
			/* birthday */
			tempDate = QDateTime::fromString(gcal_contact_get_birthday(contact),"yyyy-MM-dd");
			if (!tempDate.isValid())
				tempDate = QDateTime::fromString(gcal_contact_get_birthday(contact),"--MM-dd");
			addressee.setBirthday(tempDate);
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

gcal_phone_type akonadiPhoneTypeToGoogleLabel(KABC::PhoneNumber::Type type) {
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


gcal_address_type akonadiAddressTypeToGoogleLabel(KABC::Address::Type type) {
	switch ( type ) {
		case KABC::Address::Home:
			return A_HOME;
			break;
		case KABC::Address::Work:
			return A_WORK;
			break;
		default:
			return A_OTHER;
	}
}

gcal_address_type prefAkonadiAddressTypeToGoogleLabel(KABC::Address::Type type) {
	switch ( type ) {
		case KABC::Address::Home + KABC::Address::Pref:
			return A_HOME;
			break;
		case KABC::Address::Work + KABC::Address::Pref:
			return A_WORK;
			break;
		case KABC::Address::Dom + KABC::Address::Pref:
		case KABC::Address::Intl + KABC::Address::Pref:
		case KABC::Address::Postal + KABC::Address::Pref:
		case KABC::Address::Parcel + KABC::Address::Pref:
			return A_OTHER;
			break;
		default:
			return A_INVALID;
	}
}

void GoogleContactsResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{

	Q_UNUSED(collection);

	KABC::Addressee addressee;
	KABC::Address address;
	gcal_contact_t contact;
	gcal_structured_subvalues_t structured_entry;
	int structured_entry_nr, structured_entry_count;
	gcal_address_type temp_address_type;
	QString temp,temp2;
	QDateTime tempDate;
	KUrl tempUrl;
	QStringList listEmail;
	QStringList listImTypes;
	listImTypes << "aim" << "msn" << "yahoo" << "skype" << "icq" << "xmpp" << "qq";
	QStringList::const_iterator email;
	QByteArray t_byte,t_byte2;
	QList<KABC::Address> listAddress;
	QList<KABC::PhoneNumber> listNumber;
	KABC::Picture photo;
	int result;
	int num_elem;
	int j;
	bool ok;
	bool fill_entry;

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

	/* structured name */
	fill_entry = 1;
	structured_entry = gcal_contact_get_structured_name(contact);
	gcal_contact_delete_structured_entry(structured_entry,NULL,NULL);
	/* First Name */
	temp = addressee.givenName();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "givenName", t_byte.data());
		fill_entry = 0;
	}
	/* Additional Name */
	temp = addressee.additionalName();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "additionalName", t_byte.data());
		fill_entry = 0;
	}
	/* Family Name */
	temp = addressee.familyName();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "familyName", t_byte.data());
		fill_entry = 0;
	}
	/* Prefix */
	temp = addressee.prefix();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "namePrefix", t_byte.data());
		fill_entry = 0;
	}
	/* Suffix */
	temp = addressee.suffix();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "nameSuffix", t_byte.data());
		fill_entry = 0;
	}

	if (fill_entry)
	{
		/* This 2 fields are required! */
		temp = addressee.realName();
		t_byte = temp.toUtf8();
		gcal_contact_set_title(contact, t_byte.data());
	}

	/* nickname */
	temp = addressee.nickName();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_nickname(contact, t_byte.data());
	}

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
		structured_entry = gcal_contact_get_structured_address(contact);
		gcal_contact_delete_structured_entry(structured_entry,gcal_contact_get_structured_address_count_obj(contact),gcal_contact_get_structured_address_type_obj(contact));
		foreach (const KABC::Address &address, listAddress) {
			if ((temp_address_type=prefAkonadiAddressTypeToGoogleLabel(address.type())) != A_INVALID)
				gcal_contact_set_pref_structured_address(contact,gcal_contact_get_structured_address_count(contact));
			else
				temp_address_type=akonadiAddressTypeToGoogleLabel(address.type());
			structured_entry_nr = gcal_contact_set_structured_address_nr(contact,temp_address_type);
			structured_entry_count = gcal_contact_get_structured_address_count(contact);
			fill_entry = 1;
			/* Street */
			temp = address.street();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "street", t_byte.data());
				fill_entry = 0;
			}

			/* PO Box */
			temp = address.postOfficeBox();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "pobox", t_byte.data());
				fill_entry = 0;
			}

			/* City */
			temp = address.locality();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "city", t_byte.data());
				fill_entry = 0;
			}

			/* Region */
			temp = address.region();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "region", t_byte.data());
				fill_entry = 0;
			}

			/* Postcode */
			temp = address.postalCode();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "postcode", t_byte.data());
				fill_entry = 0;
			}

			/* Country */
			temp = address.country();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "country", t_byte.data());
				fill_entry = 0;
			}
			/* Extended */
			temp = address.extended();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "formattedAddress", t_byte.data());
				if (fill_entry)
					gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "street", t_byte.data());
			}
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
							akonadiPhoneTypeToGoogleLabel(number.type()));
			}
		}
	}

	ok = 0;
	foreach (temp, listImTypes) {
		temp2 = addressee.custom(QString::fromUtf8("messaging/").append(temp),"All");
		if (temp2.length())
			ok = 1;
	}
	if (ok) {
		gcal_contact_delete_im(contact);
		foreach (temp, listImTypes) {
			temp2 = addressee.custom(QString::fromUtf8("messaging/").append(temp),"All");
			if (temp2.length()) {
				if ( temp == QString::fromUtf8("xmpp") )
					temp = QString::fromUtf8("jabber");
				t_byte = temp.toUpper().toUtf8();
				t_byte2 = temp2.toUtf8();
				gcal_contact_add_im(contact, t_byte.data(), t_byte2.data(), I_OTHER, 0);
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

	temp = addressee.custom("KADDRESSBOOK","X-Profession");
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_occupation(contact, t_byte.data());
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

	tempUrl = addressee.url();
	temp = tempUrl.prettyUrl();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_homepage(contact, t_byte.data());
	}

	tempUrl = addressee.custom("KADDRESSBOOK","BlogFeed");
	temp = tempUrl.prettyUrl();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_blog(contact, t_byte.data());
	}

	tempDate = addressee.birthday();
	temp = tempDate.toString("yyyy-MM-dd");
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_birthday(contact, t_byte.data());
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
		ResourceBase::cancelTask(QString("Failed adding contact!"));
		ResourceBase::doSetOnline(false);
		return;
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
	QStringList listImTypes;
	listImTypes << "aim" << "msn" << "yahoo" << "skype" << "icq" << "xmpp" << "qq";
	QStringList listEmail;
	QStringList::const_iterator email;
	QList<KABC::PhoneNumber> listNumber;
	gcal_contact_t contact;
	gcal_structured_subvalues_t structured_entry;
	int structured_entry_nr, structured_entry_count;
	gcal_address_type temp_address_type;
	QByteArray t_byte,t_byte2;
	QString temp,temp2;
	QDateTime tempDate;
	KUrl tempUrl;
	KABC::Picture photo;
	int result;
	int num_elem;
	int j;
	bool ok;
	bool fill_entry;

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
		exit(1);
	}

	/* structured name */
	fill_entry = 1;
	structured_entry = gcal_contact_get_structured_name(contact);
	gcal_contact_delete_structured_entry(structured_entry,NULL,NULL);
	/* First Name */
	temp = addressee.givenName();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "givenName", t_byte.data());
		fill_entry = 0;
	}
	/* Additional Name */
	temp = addressee.additionalName();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "additionalName", t_byte.data());
		fill_entry = 0;
	}
	/* Family Name */
	temp = addressee.familyName();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "familyName", t_byte.data());
		fill_entry = 0;
	}
	/* Prefix */
	temp = addressee.prefix();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "namePrefix", t_byte.data());
		fill_entry = 0;
	}
	/* Suffix */
	temp = addressee.suffix();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_structured_entry(structured_entry, 0, 1, "nameSuffix", t_byte.data());
		fill_entry = 0;
	}

	if (fill_entry)
	{
		/* This 2 fields are required! */
		temp = addressee.realName();
		t_byte = temp.toUtf8();
		gcal_contact_set_title(contact, t_byte.data());
	}
// 	/* All the Name Parts */
// 	temp = addressee.realName();
// 	if (temp.length()) {
// 		t_byte = temp.toUtf8();
// 		gcal_contact_set_structured_entry(structured_entry,0,1, "fullName", t_byte.data());
// 	}

	/* nickname */
	temp = addressee.nickName();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_nickname(contact, t_byte.data());
	}

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
		structured_entry = gcal_contact_get_structured_address(contact);
		gcal_contact_delete_structured_entry(structured_entry,gcal_contact_get_structured_address_count_obj(contact),gcal_contact_get_structured_address_type_obj(contact));
		foreach (const KABC::Address &address, listAddress) {
			if ((temp_address_type=prefAkonadiAddressTypeToGoogleLabel(address.type())) != A_INVALID)
				gcal_contact_set_pref_structured_address(contact,gcal_contact_get_structured_address_count(contact));
			else
				temp_address_type=akonadiAddressTypeToGoogleLabel(address.type());
			structured_entry_nr = gcal_contact_set_structured_address_nr(contact,temp_address_type);
			structured_entry_count = gcal_contact_get_structured_address_count(contact);
			fill_entry = 1;
			/* Street */
			temp = address.street();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "street", t_byte.data());
				fill_entry = 0;
			}

			/* PO Box */
			temp = address.postOfficeBox();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "pobox", t_byte.data());
				fill_entry = 0;
			}

			/* City */
			temp = address.locality();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "city", t_byte.data());
				fill_entry = 0;
			}

			/* Region */
			temp = address.region();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "region", t_byte.data());
				fill_entry = 0;
			}

			/* Postcode */
			temp = address.postalCode();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "postcode", t_byte.data());
				fill_entry = 0;
			}

			/* Country */
			temp = address.country();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "country", t_byte.data());
				fill_entry = 0;
			}
			/* Extended */
			temp = address.extended();
			if (temp.length()) {
				t_byte = temp.toUtf8();
				gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "formattedAddress", t_byte.data());
				if (fill_entry)
					gcal_contact_set_structured_entry(structured_entry, structured_entry_nr, structured_entry_count, "street", t_byte.data());
			}
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
							akonadiPhoneTypeToGoogleLabel(number.type()));
			}
		}
	}

	ok = 0;
	foreach (temp, listImTypes) {
		temp2 = addressee.custom(QString::fromUtf8("messaging/").append(temp),"All");
		if (temp2.length())
			ok = 1;
	}

	if (ok) {
		gcal_contact_delete_im(contact);
		foreach (temp, listImTypes) {
			temp2 = addressee.custom(QString::fromUtf8("messaging/").append(temp),"All");
			if (temp2.length()) {
				if ( temp == QString::fromUtf8("xmpp") )
					temp = QString::fromUtf8("jabber");
				t_byte = temp.toUpper().toUtf8();
				t_byte2 = temp2.toUtf8();
				gcal_contact_add_im(contact, t_byte.data(), t_byte2.data(),I_OTHER, 0);
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

	temp = addressee.custom("KADDRESSBOOK","X-Profession");
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_occupation(contact, t_byte.data());
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

	tempUrl = addressee.url();
	temp = tempUrl.prettyUrl();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_homepage(contact, t_byte.data());
	}

	tempUrl = addressee.custom("KADDRESSBOOK","BlogFeed");
	temp = tempUrl.prettyUrl();
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_blog(contact, t_byte.data());
	}

	tempDate = addressee.birthday();
	temp = tempDate.toString("yyyy-MM-dd");
	if (temp.length()) {
		t_byte = temp.toUtf8();
		gcal_contact_set_birthday(contact, t_byte.data());
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
		ResourceBase::cancelTask(QString("Failed editing contact!"));
		ResourceBase::doSetOnline(false);
		return;
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
		exit(1);
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
