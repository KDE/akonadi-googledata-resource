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

#include "gcalresource.h"
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
#include <gcal_status.h>
}

using namespace Akonadi;

GCalResource::GCalResource( const QString &id )
	: ResourceBase(id)
{
	new SettingsAdaptor( Settings::self() );
	QDBusConnection::sessionBus().registerObject(
		QLatin1String( "/Settings" ), Settings::self(),
		QDBusConnection::ExportAdaptors );

	changeRecorder()->itemFetchScope().fetchFullPayload();

	if (!(gcal = gcal_new(GCALENDAR))) 
		exit(1);
	gcal_set_store_xml(gcal, 1);
	all_events.length = 0;
	all_events.entries = NULL;
}

GCalResource::~GCalResource()
{
	gcal_cleanup_events(&all_events);
	pending.clear();
	deleted.clear();
}

void GCalResource::retrieveTimestamp(QString &timestamp)
{
	timestamp = Settings::self()->timestamp();
}

void GCalResource::saveTimestamp(QString &timestamp)
{
 	Settings::self()->setTimestamp(timestamp);
 	Settings::self()->writeConfig();
}


void GCalResource::retrieveCollections()
{
	if(!authenticated) {
		kError() << "No authentication for Google Calendar available";
                const QString message = i18nc("@info: status",
					      "Not yet authenticated for "	
					      "use of Google Calendar");

		emit error(message);
		
                emit status(Broken, message);
		return;
	}

	Collection c;
        c.setParent(Collection::root());
        c.setRemoteId("google-calendar");
	c.setName(name());

	QStringList mimeTypes;
	mimeTypes << "text/calendar";
	c.setContentMimeTypes(mimeTypes);
	
	Collection::List list;
	list << c;
	collectionsRetrieved(list);
}

void GCalResource::retrieveItems( const Akonadi::Collection &collection )
{
	Q_UNUSED( collection );

}

bool GCalResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED( parts );
	Q_UNUSED( item );
	return true;
}

void GCalResource::aboutToQuit()
{
	// TODO: any cleanup you need to do while there is still an active
	// event loop. The resource will terminate after this method returns
}

void GCalResource::doSetOnline(bool online)
{

}

int GCalResource::getUpdated(char *timestamp)
{

}

void GCalResource::configure( WId windowId )
{
	Q_UNUSED( windowId );

}

void GCalResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{
	Q_UNUSED(collection);
}

void GCalResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED(parts);
}

void GCalResource::itemRemoved( const Akonadi::Item &item )
{

}

AKONADI_RESOURCE_MAIN( GCalResource )

#include "gcalresource.moc"
