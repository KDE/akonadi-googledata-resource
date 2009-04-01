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

GCalResource::GCalResource( const QString &id )
	: ResourceBase(id)
{
	new SettingsAdaptor( Settings::self() );
	QDBusConnection::sessionBus().registerObject(
		QLatin1String( "/Settings" ), Settings::self(),
		QDBusConnection::ExportAdaptors );

	changeRecorder()->itemFetchScope().fetchFullPayload();

}

GCalResource::~GCalResource()
{

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
