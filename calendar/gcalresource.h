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
#ifndef GCALRESOURCE_H
#define GCALRESOURCE_H

#include <akonadi/resourcebase.h>
#include "googledata.h"

class GCalResource : public Akonadi::ResourceBase,
		     public Akonadi::AgentBase::Observer,
		     public GoogleData
{
Q_OBJECT
public:
	GCalResource( const QString &id );
	~GCalResource();

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
	/* Event array */
	struct gcal_event_array all_events;
	/* Event itens update lists */
	Akonadi::Item::List pending;
	Akonadi::Item::List deleted;

};

#endif
