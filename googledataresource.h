#ifndef GOOGLEDATARESOURCE_H
#define GOOGLEDATARESOURCE_H

#include <akonadi/resourcebase.h>
extern "C" {
#include <gcalendar.h>
#include <gcontact.h>
}

class googledataResource : public Akonadi::ResourceBase,
                           public Akonadi::AgentBase::Observer
{
Q_OBJECT
public:
	googledataResource( const QString &id );
	~googledataResource();

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

	/* Google data context: holds user account name/password */
	gcal_t gcal;
	/* Contact array */
	struct gcal_contact_array all_contacts;
};

#endif
