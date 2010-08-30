/***********************************************************************/
/* googledata.h 						       */
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

#ifndef __GOOGLE_DATA__
#define __GOOGLE_DATA__

#include <kwallet.h>
#include "dlgGoogleDataConf.h"
class QString;
typedef long unsigned int WId;

extern "C" {
#include <libgcal/gcalendar.h>
#include <libgcal/gcontact.h>
}

class GoogleData {

public:
	GoogleData();
	virtual ~GoogleData();

protected:

	int saveToWallet(const QString &user, const QString &pass,
			 const WId &window,
			 const QString &folder = "akonadigoogle",
			 const QString &awallet = "kdewallet");


	int retrieveFromWallet(QString &user, QString &pass,
			       const WId &window,
			       const QString &folder = "akonadigoogle");

	int authenticate(const QString &user, const QString &password);

	/* KWallet object pointer */
	KWallet::Wallet *wallet;

	/* Config dialog */
	dlgGoogleDataConf *dlgConf;

	/* Flag with authentication */
	bool authenticated;

	/* Google data context: holds user account name/password */
	gcal_t gcal;

};

#endif
