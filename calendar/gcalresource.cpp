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
 * - fix missing fields on 'add' event
 * - implement and test edit event
 * - support for recurrent events (it will require changes on libgcal)
 */

#include "gcalresource.h"
#include "settings.h"
#include "settingsadaptor.h"

#include <QtDBus/QDBusConnection>
#include <kabc/addressee.h>
#include <kabc/phonenumber.h>
#include <kabc/key.h>
#include <kabc/errorhandler.h>
#include <kcal/event.h>
#include <kdatetime.h>
#include <qstring.h>
#include <KWindowSystem>
#include <akonadi/changerecorder.h>
#include <akonadi/itemfetchscope.h>
#include <KUrl>
#include <kdatetime.h>
#include <boost/shared_ptr.hpp>
typedef boost::shared_ptr<KCal::Incidence> IncidencePtr;

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
    if (!authenticated) {
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
	Q_UNUSED(collection);
	Item::List items;
	int result;
	gcal_event_t event;
	QString timestamp;
	QByteArray t_byte;

	kError() << "\n............. retrieveItems ...........\n";
	if (!authenticated) {
		kError() << "No authentication for Google calendar available";
		const QString message = i18nc("@info:status",
					      "Not yet authenticated for"
					      " use of Google calendar");
		emit error(message);
		emit status(Broken, message);
		return;
	}

	/* Query by updated */
// 	retrieveTimestamp(timestamp);
// 	t_byte = timestamp.toLocal8Bit();
// 	if (t_byte.length() > TIMESTAMP_SIZE) {
// 		//TODO: implement getUpdated
// 		//result = getUpdated(t_byte.data());
// 		return;
// 	}
	kError() << "First retrieve";

	if ((result = gcal_get_events(gcal, &all_events)))
		exit(1);

	/* GCalendar returns last updated entry as first element */
	event = gcal_event_element(&all_events, 0);
	if (!event) {
		kError() << "Failed to retrieve last updated event.";
		const QString message = i18nc("@info:status",
					      "Failed getting last updated"
					      " event");
		emit error(message);
		emit status(Broken, message);
		return;

	}

	timestamp = gcal_event_get_updated(event);
	saveTimestamp(timestamp);

	/* Each google entry has a unique ID and edit_url */
	for (size_t i = 0; i < all_events.length; ++i) {
		Item item(QLatin1String("text/calendar"));
		KCal::Event *kevent;
		kevent = new KCal::Event;
		QString temp;
		event = gcal_event_element(&all_events, i);

		temp = gcal_event_get_title(event);
		kevent->setSummary(temp);

		temp = gcal_event_get_where(event);
		kevent->setLocation(temp);

		KCal::Incidence::Status status;
		if (gcal_event_is_deleted(event))
			status = KCal::Incidence::StatusCanceled;
		else
			status = KCal::Incidence::StatusConfirmed;
		kevent->setStatus(status);

		temp = gcal_event_get_content(event);
		kevent->setDescription(temp);

		KDateTime start, end;
		temp = gcal_event_get_start(event);
		start = start.fromString(temp, KDateTime::ISODate);
		temp = gcal_event_get_end(event);
		end = end.fromString(temp, KDateTime::ISODate);
		kevent->setDtStart(start);
		kevent->setDtEnd(end);

		qDebug() << "start: " << start.dateTime()
			 << "\tend: " << end.dateTime();


		/* remoteID: edit_url */
		KUrl urlEtag(gcal_event_get_url(event));
		item.setRemoteId(urlEtag.url());
		item.setPayload(IncidencePtr(kevent));

		items << item;
	}

	itemsRetrieved(items);
	kError() << "\n............. done retrieveItems! ...........\n";
}

bool GCalResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED(parts);
	Q_UNUSED(item);
	return true;
}

void GCalResource::aboutToQuit()
{
	// TODO: any cleanup you need to do while there is still an active
	// event loop. The resource will terminate after this method returns
}

void GCalResource::doSetOnline(bool online)
{
	Q_UNUSED(online);
}

int GCalResource::getUpdated(char *timestamp)
{
	Q_UNUSED(timestamp);

	return -1;
}

void GCalResource::configure( WId windowId )
{
	int result = 0;
	if (windowId && dlgConf)
		KWindowSystem::setMainWindow(dlgConf, windowId);

	dlgConf->exec();
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

void GCalResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{
	Q_UNUSED(collection);

    gcal_event_t event;
    KCal::Event *kevent;
    IncidencePtr ptrEvent;
    QByteArray t_byte;
    QString temp;
    KDateTime time;
    //TODO: test for recurrence (and ignore creation for while)

    if (!authenticated) {
        kError() << "No authentication for Google calendar available";
        const QString message = i18nc("@info:status",
                          "Not yet authenticated for"
                          " use of Google calendar");
        emit error(message);
        emit status(Broken, message);
        return;
    }

    if (item.hasPayload<IncidencePtr>()) {
	    ptrEvent = item.payload<IncidencePtr>();
	    kevent = dynamic_cast<KCal::Event *>(ptrEvent.get());
    } else {
        kError() << "Add without payload!";
        const QString message = i18nc("@info:status",
                          "No payload to add event");
        emit error(message);
        emit status(Broken, message);
        return;
    }

    if (!(event = gcal_event_new(NULL))) {
        kError() << "Memory allocation error!";
        const QString message = i18nc("@info:status",
                      "Failed to create gcal_event");
        emit error(message);
        emit status(Broken, message);
        return;
    }

    /* FIXME: for some reason the event is missing these fields:
     * summary, description, location.
     */
    temp = kevent->summary();
    if(!temp.length()) {
        t_byte = temp.toLocal8Bit();
        gcal_event_set_title(event, t_byte);
    }

    temp = kevent->description();
    if(!temp.length()) {
        t_byte = temp.toLocal8Bit();
        gcal_event_set_content(event, t_byte);
    }

    temp = kevent->location();
    if(!temp.length()) {
        t_byte = temp.toLocal8Bit();
        gcal_event_set_where(event, t_byte);
    }

    /* Only this fields are being successfuly extracted */
    time = kevent->dtStart();
    temp = time.toString(KDateTime::ISODate);
    t_byte = temp.toLocal8Bit();
    gcal_event_set_start(event, t_byte.data());

    time = kevent->dtEnd();
    temp = time.toString(KDateTime::ISODate);
    t_byte = temp.toLocal8Bit();
    gcal_event_set_end(event, t_byte.data());

    if ((gcal_add_event(gcal, event))) {
        kError() << "Failed adding new calendar"
                << "title:" << kevent->summary();
        const QString message = i18nc("@info:status",
                            "failed adding new calendar");
        emit error(message);
        emit status(Broken, message);
    }

    KUrl url(gcal_event_get_url(event));
    Item newItem(item);
    /* Either 'new' or crash... */
    newItem.setPayload(IncidencePtr(new KCal::Event(*kevent)));
    newItem.setRemoteId(url.url());
    changeCommitted(newItem);

    gcal_event_delete(event);
}

void GCalResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
	Q_UNUSED(parts);
	Q_UNUSED(item);
}

void GCalResource::itemRemoved( const Akonadi::Item &item )
{
	Q_UNUSED(item);

    gcal_event_t event;
    QString temp;
    QByteArray t_byte;

    kDebug() << "Deleting one item ... ";

    if(!authenticated) {
        kError() << "No authentication for Google calendar";
        const QString message = i18nc("@info:status",
                                "Not yet authenticated for "
                                "use of Google calendar");
        emit error(message);
        emit status(Broken, message);
    }

    if (!(event = gcal_event_new(NULL))) {
        kError() << "Memory allocation error!";
        const QString message = i18nc("@info:status",
                                "Failed to create gcal_event");
        emit error(message);
        emit status(Broken, message);
    }

    KUrl url(item.remoteId());
    temp = url.url();
    t_byte = temp.toAscii();
    gcal_event_set_url(event, t_byte.data());

    if (gcal_erase_event(gcal, event)) {
        kError() << "Failed deleting calendar";
        const QString message = i18nc("@info:status",
                                "Failed deleting new calendar");
        emit error(message);
        emit status(Broken, message);
    }

    gcal_event_delete(event);

    changeProcessed();
    kDebug() << "done deleting!!";
}

AKONADI_RESOURCE_MAIN( GCalResource )

#include "gcalresource.moc"
