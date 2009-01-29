#include "googledataresource.h"

#include "settings.h"
#include "settingsadaptor.h"

#include <QtDBus/QDBusConnection>
#include <kabc/addressee.h>
#include <kabc/phonenumber.h>

extern "C" {
#include <gcalendar.h>
#include <gcontact.h>
#include <gcal_status.h>
}

using namespace Akonadi;

googledataResource::googledataResource( const QString &id )
	: ResourceBase( id )
{
	new SettingsAdaptor( Settings::self() );
	QDBusConnection::sessionBus().registerObject( QLatin1String( "/Settings" ),
						      Settings::self(), QDBusConnection::ExportAdaptors );


	if (!(gcal = gcal_new(GCONTACT)))
		exit(1);
	gcal_set_store_xml(gcal, 1);
}

googledataResource::~googledataResource()
{
	gcal_delete(gcal);
	gcal_cleanup_contacts(&all_contacts);
}

void googledataResource::retrieveCollections()
{
	// TODO: this method is called when Akonadi wants to have all the
	// collections your resource provides.
	// Be sure to set the remote ID and the content MIME types
}

void googledataResource::retrieveItems( const Akonadi::Collection &collection )
{
	Q_UNUSED( collection );

	Item::List items;
	int result;

	/* Downloading the contacts can be slow and it is blocking. Will
	 * it mess up with akonadi?
	 */
	if ((result = gcal_get_contacts(gcal, &all_contacts)))
		exit(1);

	/* Each google entry has a unique ID and edit_url */
	for (size_t i = 0; i < all_contacts.length; ++i) {

		Item item(QLatin1String("what_is_the_mime_type?"));
		gcal_contact_t contact = gcal_contact_element(&all_contacts, i);
		item.setRemoteId(gcal_contact_get_id(contact));
	}

	itemsRetrieved(items);
}

bool googledataResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED( parts );
	const QString entry_id = item.remoteId();
	QString temp;
	Item newItem(item);
	gcal_contact_t contact;
	KABC::Addressee addressee;
	KABC::PhoneNumber number;

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

			/* TODO: add ETag/etc  on libgcal */

			newItem.setPayload<KABC::Addressee>(addressee);
			return true;
		}

	}

	return false;
}

void googledataResource::aboutToQuit()
{
	// TODO: any cleanup you need to do while there is still an active
	// event loop. The resource will terminate after this method returns
}

void googledataResource::configure( WId windowId )
{
	Q_UNUSED( windowId );

	/* TODO:
	 * what kind of dialog to collect google acount username + password ?
	 */
	synchronize();
}

void googledataResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{
	Q_UNUSED( item );
	Q_UNUSED( collection );

	// TODO: this method is called when somebody else, e.g. a client application,
	// has created an item in a collection managed by your resource.

	// NOTE: There is an equivalent method for collections, but it isn't part
	// of this template code to keep it simple
}

void googledataResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED( item );
	Q_UNUSED( parts );

	// TODO: this method is called when somebody else, e.g. a client application,
	// has changed an item managed by your resource.

	// NOTE: There is an equivalent method for collections, but it isn't part
	// of this template code to keep it simple
}

void googledataResource::itemRemoved( const Akonadi::Item &item )
{
	Q_UNUSED( item );

	// TODO: this method is called when somebody else, e.g. a client application,
	// has deleted an item managed by your resource.

	// NOTE: There is an equivalent method for collections, but it isn't part
	// of this template code to keep it simple
}

AKONADI_RESOURCE_MAIN( googledataResource )

#include "googledataresource.moc"
