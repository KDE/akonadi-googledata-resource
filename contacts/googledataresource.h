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
#ifndef GOOGLEDATARESOURCE_H
#define GOOGLEDATARESOURCE_H

#include <akonadi/resourcebase.h>
#include "googledata.h"

class GoogleContactsResource : public Akonadi::ResourceBase,
			       public Akonadi::AgentBase::Observer,
			       public GoogleData
{
Q_OBJECT
public:
	GoogleContactsResource( const QString &id );
	~GoogleContactsResource();

public Q_SLOTS:
	virtual void configure( WId windowId );

protected Q_SLOTS:
	void retrieveCollections();
	void retrieveItems( const Akonadi::Collection &col );
	bool retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts );

protected:
	virtual void aboutToQuit();
	virtual void itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection );
	virtual void itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts );
	virtual void itemRemoved( const Akonadi::Item &item );
	virtual void doSetOnline(bool online);

	void retrieveTimestamp(QString &timestamp);
	void saveTimestamp(QString &timestamp);

	int getUpdated(char *timestamp);
	/* Contact array */
	struct gcal_contact_array all_contacts;
	/* Contact itens update lists */
	Akonadi::Item::List pending;
	Akonadi::Item::List deleted;

private:
	bool reverseName;

};

#endif
