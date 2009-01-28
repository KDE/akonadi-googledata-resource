#include "googledataresource.h"

#include "settings.h"
#include "settingsadaptor.h"

#include <QtDBus/QDBusConnection>

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

	// TODO: this method is called when Akonadi wants to know about all the
	// items in the given collection. You can but don't have to provide all the
	// data for each item, remote ID and MIME type are enough at this stage.
	// Depending on how your resource accesses the data, there are several
	// different ways to tell Akonadi when you are done.
}

bool googledataResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED( item );
	Q_UNUSED( parts );

	// TODO: this method is called when Akonadi wants more data for a given item.
	// You can only provide the parts that have been requested but you are allowed
	// to provide all in one go

	return true;
}

void googledataResource::aboutToQuit()
{
	// TODO: any cleanup you need to do while there is still an active
	// event loop. The resource will terminate after this method returns
}

void googledataResource::configure( WId windowId )
{
	Q_UNUSED( windowId );

	// TODO: this method is usually called when a new resource is being
	// added to the Akonadi setup. You can do any kind of user interaction here,
	// e.g. showing dialogs.
	// The given window ID is usually useful to get the correct
	// "on top of parent" behavior if the running window manager applies any kind
	// of focus stealing prevention technique
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
