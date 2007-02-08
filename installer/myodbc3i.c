/* Copyright (C) 2000-2005 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   There are special exceptions to the terms and conditions of the GPL as it
   is applied to this software. View the full text of the exception in file
   EXCEPTIONS in the directory of this software distribution.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*!
  \file   myodbc3i.c
  \author Peter Harvey <peterh@mysql.com>
  \brief  This program will aid installers when installing/uninstalling
          MyODBC.

          This program can; register/deregister a myodbc driver and create
          a sample dsn. The key thing here is that it does this with 
          cross-platform code - thanks to the ODBC installer API. This is
          most useful to those creating installers (apps using myodbc or 
          myodbc itself).

          For example; this program is used in the postinstall script
          of the MyODBC for OSX installer package. 
*/

/*!
    \note   This program uses MYODBCUtil in similar fashion to the *driver*
            but does not use it the way the setup library does. 
*/

#include <stdio.h>

#ifndef TRUE
    #define TRUE 1
    #define FALSE 0
#endif

#ifdef WIN32
    #include <windows.h>
#else
    #include <ltdl.h>
#endif

#include "../util/MYODBCUtil.h"

#ifdef USE_IODBC
#include <iodbcinst.h>
#else
#include <odbcinst.h>
#endif

/*! Our syntax. This is likely to expand over time. */
char *szSyntax =
"\n" \
"+----------------------------------------------------------+\n" \
"|                          myodbc3i                        |\n" \
"+----------------------------------------------------------+\n" \
"\n" \
"  Use this program to manage ODBC system information from the\n" \
"  command-line. This is particularly useful for installation \n" \
"  processes. It is also useful for testing the abstraction/\n" \
"  portability of ODBC system information provided by the  \n" \
"  MYODBCUtil library.\n" \
"\n" \
"+----------------------------------------------------------+\n" \
"|                         U S A G E                        |\n" \
"+----------------------------------------------------------+\n" \
"\n" \
"  $ myodbc3i <action> <object> <option>\n" \
"\n" \
"+----------------------------------------------------------+\n" \
"|                        A C T I O N                       |\n" \
"+----------------------------------------------------------+\n" \
"\n" \
"  q    Query\n" \
"  a    Add\n" \
"  e    Edit\n" \
"  r    Remove\n" \
"\n" \
"+----------------------------------------------------------+\n" \
"|                        O B J E C T                       |\n" \
"+----------------------------------------------------------+\n" \
"\n" \
"  d    Driver.\n" \
"  n    Driver or data source name. String follows this.\n" \
"  s    User & system data source(s).\n" \
"  su   User data source(s).\n" \
"  ss   System data source(s).\n" \
"\n" \
"+----------------------------------------------------------+\n" \
"|                        O P T I O N                       |\n" \
"+----------------------------------------------------------+\n" \
"\n" \
"  t    Attribute string. String of semi delim key=value pairs follows this.\n" \
"  w    Window handle. Numeric follows this. 0 to disable GUI prompts, 1 to fake a window handle (default).\n" \
"\n" \
"+----------------------------------------------------------+\n" \
"|                        S Y N T A X                       |\n" \
"+----------------------------------------------------------+\n" \
"\n" \
"  q d|s|su|ss [n] [w]\n" \
"  a d|s|su|ss t [w]\n" \
"  e s|su|ss t [w]\n" \
"  r d|s|su|ss n [w]\n" \
"\n" \
"+----------------------------------------------------------+\n" \
"|                       E X A M P L E S                    |\n" \
"+----------------------------------------------------------+\n" \
"\n" \
"  List all data source names;\n" \
"    $ myodbc3i -q -s\n" \
"  List data source attributes for MyODBC;\n" \
"    $ myodbc3i -q -s -n\"MySQL ODBC 3.51 Driver\"\n" \
"  Register a driver;\n" \
"    $ myodbc3i -a -d -t\"MySQL ODBC 3.51 Driver;DRIVER=/usr/lib/libmyodbc3.so;SETUP=/usr/lib/libmyodbc3S.so\"\n" \
"  Create a user data source name;\n" \
"    $ myodbc3i -a -su -t\"DSN=MyDSN;DRIVER=MySQL ODBC 3.51 Driver;SERVER=localhost;UID=pharvey\"\n" \
"\n" \
"Enjoy\n" \
"Peter Harvey\n";

/*
    Register Driver for Various Platforms
    
    XP
    
        $ myodbc3i -a -d -t"MySQL ODBC 3.51 Driver;DRIVER=myodbc3.dll;SETUP=myodbc3S.dll"
        
        OR for old GUI...
        
        $ myodbc3i -a -d -t"MySQL ODBC 3.51 Driver;DRIVER=myodbc3.dll;SETUP=myodbc3.dll"
        
    OSX

          At least some of the functions dealing with the
          odbc system information are case sensitive
          (contrary to the ODBC spec.). They appear to like
          leading caps.
          
        $ myodbc3i -a -d -t"MySQL ODBC 3.51 Driver;Driver=/usr/lib/libmyodbc3.so;Setup=/usr/lib/libmyodbc3S.dylib"
        
    UNIX/Linux
    
        $ myodbc3i -a -d -t"MySQL ODBC 3.51 Driver;DRIVER=/usr/lib/libmyodbc3.so;SETUP=/usr/lib/libmyodbc3S.so"

*/

char    cAction         = '\0';
char    cObject         = '\0';
char    cObjectSub      = '\0';
long    nWnd            = 1;
char *  pszName         = NULL;
char *  pszAttributes   = NULL;

char    szBuffer[32000];
int     nBuffer = 32000;

#if defined(WIN32)
/*!
    \brief  Get the last *Windows* error and display string ver of it.
*/
void doPrintLastErrorString() 
{
    LPVOID pszMsg;

    FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   NULL,
                   GetLastError(),
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPTSTR)&pszMsg,
                   0, 
                   NULL );
    fprintf( stderr, pszMsg );
    LocalFree( pszMsg );
}
#endif

/*!
    \brief  Prints any installer errors sitting in the installers
            error queue.

            General purpose error dump function for installer 
            errors. Hopefully; it provides useful information about
            any failed installer call.

    \note   Does not try to process all 8 possible records from the queue.
*/            
void doPrintInstallerError( char *pszFile, int nLine )
{
    WORD      nRecord = 1;
    DWORD     nError;
    char      szError[SQL_MAX_MESSAGE_LENGTH];
    RETCODE   nReturn;

    nReturn = SQLInstallerError( nRecord, &nError, szError, SQL_MAX_MESSAGE_LENGTH - 1, 0 );
    if ( SQL_SUCCEEDED( nReturn ) )
        fprintf( stderr, "[%s][%d][ERROR] ODBC Installer error %d: %s\n", pszFile, nLine, (int)nError, szError );
    else
        fprintf( stderr, "[%s][%d][ERROR] ODBC Installer error (unknown)\n", pszFile, nLine );
}


int doQuery();
int doQueryDriver();
int doQueryDriverName();
int doQueryDataSource();
int doQueryDataSourceName( UWORD nScope );
int doAdd();
int doAddDriver();
int doAddDataSource();
int doEdit();
int doEditDriver();
int doEditDataSource();
int doRemove();
int doRemoveDriver();
int doRemoveDataSource();
int doRemoveDataSourceName();
int doConfigDataSource( WORD nRequest );
/* added here to address build issue on osx 10.4 */
BOOL INSTAPI AppleConfigDSN( HWND hWnd, WORD nRequest, LPCSTR pszDriver, LPCSTR pszAttributes );

/*!
    \brief  This is the entry point to this program.
            
    \note   More features/args will probably be added in the future.
*/
int main( int argc, char *argv[] )
{
    int nArg;
    int nReturn;

    /*
        help; possible newbie
    */
    if ( argc <= 1 )
    {
        printf( szSyntax );
        exit( 1 );
    }

    /* 
        parse args 
    */
    for ( nArg = 1; nArg < argc; nArg++ )
    {
        if ( argv[nArg][0] == '-' )
        {
            switch ( argv[nArg][1] )
            {
                /* actions */
                case 'e':
#ifdef __APPLE__
                    printf( "[WARNING] The dialogs on OSX may have serious focus problems being invoked this way.\nThis has yet to be corrected.\nConsider using MYODBCConfig.\n" );
#endif
                case 'q':
                case 'a':
                case 'r':
                    cAction = argv[nArg][1];
                    break;
                    /* objects */
                case 'd':
                case 's':
                    cObject     = argv[nArg][1];
                    cObjectSub  = argv[nArg][2]; /* '\0' if not provided */
                    break;
                    /* options */
                case 'n':
                    pszName = &(argv[nArg][2]);
                    break;
                case 't':
                    pszAttributes = &(argv[nArg][2]);
                    break;
                case 'w':
                    nWnd = atoi( &(argv[nArg][2]) );
                    break;
                default:
                    {
                        printf( szSyntax );
                        exit( 1 );
                    }
            }
        }
        else
        {
            printf( szSyntax );
            exit( 1 );
        }
    }

    /* 
        process args
    */
    switch ( cAction )
    {
        case 'q':
            nReturn = doQuery();
            break;
        case 'a':
            nReturn = doAdd();
            break;
        case 'e':
            nReturn = doEdit();
            break;
        case 'r':
            nReturn = doRemove();
            break;
        default:  
            printf( "[%s][%d][ERROR] Invalid, or missing, action.\n", __FILE__, __LINE__ );
            exit( 1 );
    }

    return ( !nReturn ); 
}

/*!
    \brief  Show a list of drivers or data source names.

    \note   OSX

            SQLGetPrivateProfileString() is broken but fortunately the
            old call, GetPrivateProfileString() is available.

    \note   XP

            SQLGetPrivateProfileString() with a NULL 1st arg does not
            return anything - ever. To return a list of drivers we can
            use SQLGetInstalledDrivers() but no alternative in ODBC
            installer for getting a list of data source names.
*/
int doQuery()
{
    switch ( cObject )
    {
        case 'd':
            return doQueryDriver();
        case 's':
            return doQueryDataSource();
        default:
            fprintf( stderr, "[%s][%d][ERROR] Missing or invalid object type specified.\n", __FILE__, __LINE__ );
            return 0;
    }
}

int doQueryDriver()
{
    char *psz;

    if ( pszName )
        return doQueryDriverName();

    if ( !MYODBCUtilGetDriverNames( szBuffer, nBuffer ) )
    {
        doPrintInstallerError( __FILE__, __LINE__ );
        fprintf( stderr, "[%s][%d][WARNING] Failed to get driver names. Perhaps none installed?\n", __FILE__, __LINE__ );
        return 0;
    }

    psz = szBuffer;
    while ( *psz )
    {
        printf( "%s\n", psz );
        psz += strlen( psz ) + 1;
    }

    return 1;
}

int doQueryDriverName()
{
    int bReturn = 0; 
    MYODBCUTIL_DRIVER *pDriver = MYODBCUtilAllocDriver();

    if ( !MYODBCUtilReadDriver( pDriver, pszName, NULL ) )
    {
        fprintf( stderr, "[%s][%d][WARNING] Could not load (%s)\n", __FILE__, __LINE__, pszName );
        goto doQueryDriverNameExit1;   
    }

    printf( "Name............: %s\n", pDriver->pszName );
    printf( "DRIVER..........: %s\n", pDriver->pszDRIVER );
    printf( "SETUP...........: %s\n", pDriver->pszSETUP );

    bReturn = 1;

    doQueryDriverNameExit1:
    MYODBCUtilFreeDriver( pDriver );

    return bReturn;
}

int doQueryDataSource()
{
    int     bReturn;
    char *  psz;

    switch ( cObjectSub )
    {
        case '\0':
            if ( pszName )
                return doQueryDataSourceName( ODBC_BOTH_DSN );
            else
                bReturn = MYODBCUtilGetDataSourceNames( szBuffer, nBuffer, ODBC_BOTH_DSN );
            break;
        case 'u':
            if ( pszName )
                return doQueryDataSourceName( ODBC_USER_DSN );
            else
                bReturn = MYODBCUtilGetDataSourceNames( szBuffer, nBuffer, ODBC_USER_DSN );
            break;
        case 's':
            if ( pszName )
                return doQueryDataSourceName( ODBC_SYSTEM_DSN );
            else
                bReturn = MYODBCUtilGetDataSourceNames( szBuffer, nBuffer, ODBC_SYSTEM_DSN );
            break;
        default:
            fprintf( stderr, "[%s][%d][ERROR] Invalid object type qualifier specified.\n", __FILE__, __LINE__ );
            return 0;
    }

    if ( !bReturn )
    {
        doPrintInstallerError( __FILE__, __LINE__ );
        return 0;
    }

    psz = szBuffer;
    while ( *psz )
    {
        printf( "%s\n", psz );
        psz += strlen( psz ) + 1;
    }

    return 1;
}

int doQueryDataSourceName( UWORD nScope )
{
    int bReturn = 0; 
    MYODBCUTIL_DATASOURCE *pDataSource = MYODBCUtilAllocDataSource( MYODBCUTIL_DATASOURCE_MODE_DSN_VIEW );

    /* set scope */
    switch ( nScope )
    {
        case ODBC_BOTH_DSN:
            if ( !SQLSetConfigMode( nScope ) )
                return FALSE;
            break;
        case ODBC_USER_DSN:
        case ODBC_SYSTEM_DSN:
            if ( !SQLSetConfigMode( nScope ) )
                return FALSE;
            break;
        default:
            return FALSE;
    }

    if ( !MYODBCUtilReadDataSource( pDataSource, pszName ) )
    {
        fprintf( stderr, "[%s][%d][WARNING] Could not load (%s)\n", __FILE__, __LINE__, pszName );
        goto doQueryDataSourceNameExit1;   
    }

    printf( "DSN.............: %s\n", pDataSource->pszDSN );
    printf( "DRIVER..........: %s\n", pDataSource->pszDRIVER );
    printf( "Driver file name: %s\n", pDataSource->pszDriverFileName );
    printf( "DESCRIPTION.....: %s\n", pDataSource->pszDESCRIPTION );
    printf( "SERVER..........: %s\n", pDataSource->pszSERVER );
    printf( "UID.............: %s\n", pDataSource->pszUSER );
    printf( "PWD.............: %s\n", pDataSource->pszPASSWORD );
    printf( "DATABASE........: %s\n", pDataSource->pszDATABASE );
    printf( "PORT............: %s\n", pDataSource->pszPORT );
    printf( "SOCKET..........: %s\n", pDataSource->pszSOCKET );
    printf( "STMT............: %s\n", pDataSource->pszSTMT );
    printf( "OPTION..........: %s\n", pDataSource->pszOPTION );

    bReturn = 1;

doQueryDataSourceNameExit1:
    /* unset scope */
    switch ( nScope )
    {
        case ODBC_BOTH_DSN:
            SQLSetConfigMode( ODBC_BOTH_DSN );
            break;
        case ODBC_USER_DSN:
        case ODBC_SYSTEM_DSN:
            SQLSetConfigMode( ODBC_BOTH_DSN );
            break;
    }

    MYODBCUtilFreeDataSource( pDataSource );

    return bReturn;
}

int doAdd()
{
    switch ( cObject )
    {
        case 'd':
            return doAddDriver();
        case 's':
            return doAddDataSource();
        default:
            fprintf( stderr, "[%s][%d][ERROR] Missing or invalid object type specified.\n", __FILE__, __LINE__ );
            return 0;
    }
}

/*!
    \brief  Register the driver (or just increase usage count).

    \note   XP
    
            On MS Windows (XP for example) the driver is registered in two places; 
              1) \windows\system32\odbcinst.ini
              2) registry
              Fortunately the installer calls will ensure they are both updated.

            All ODBC drivers *should* be installed in the standard location (\windows\system32) and this call
            reflects this as no path is given for the driver file.

    \note   OSX
    
            On OSX there are many odbcinst.ini files - each account has one in ~/Library/ODBC and there
            is a system wide one in /Library/ODBC. This function will register the driver in ~/Library/ODBC.

            There are at least two notable complicating factors;
              - the files are read-ony for average user so one should use sudo when doing this
              - the files do not exist until someone manually creates entries using the ODBC Administrator AND
                they are not created by iodbc installer lib when we execute this code (we get error)

            ODBC spec says that "Driver" should NOT include path 
            but path seems needed for iodbc. The implication is that
            the driver *must* be installed in /usr/lib for this to work.

            Usage Count is not returned on OSX and returned location does not seem to reflect reality.

    \note   Linux/UNIX

            ODBC spec says that "Driver" should NOT include path 
            but path seems needed for unixODBC. The implication is that
            the driver *must* be installed in /usr/lib for this to work.

            Location returned does not seem to reflect reality.
*/
int doAddDriver()
{
    char  *pszDriverInfo = NULL;
    char  szLoc[FILENAME_MAX];
    WORD  nLocLen;
    DWORD nUsageCount;
    int   nChar;

    if ( !pszAttributes )
    {
        fprintf( stderr, "[%s][%d][ERROR] Please provide driver attributes.\n", __FILE__, __LINE__ );
        return 0;
    }

    /*
        Create a oopy of pszAttributes where the ';' are replaced
        with '\0' and ensure that at least 2 '\0' are at the end.
        SQLInstallDriverEx() needs it formatted this way.
    */
    pszDriverInfo = (char *)malloc( strlen(pszAttributes) + 2 );

    for ( nChar = 0; pszAttributes[nChar]; nChar++ )
    {
        if ( pszAttributes[nChar] == ';' )
            pszDriverInfo[nChar] = '\0';
        else
            pszDriverInfo[nChar] = pszAttributes[nChar];
    }

    pszDriverInfo[nChar]    = '\0';
    pszDriverInfo[nChar++]  = '\0';

    /*
        Call ODBC installer to do the work.
    */    
    if ( !SQLInstallDriverEx( pszDriverInfo, 0, szLoc, FILENAME_MAX, &nLocLen, ODBC_INSTALL_COMPLETE, &nUsageCount ) )
    {
        doPrintInstallerError( __FILE__, __LINE__ );
        fprintf( stderr, "[%s][%d][ERROR] Failed to register driver\n", __FILE__, __LINE__ );
        free( pszDriverInfo );
        return 0;
    }

    printf( "[%s][%d][INFO] Driver registered. Usage count is %d. Location \"%s\" \n", __FILE__, __LINE__, (int)nUsageCount, szLoc );
    free( pszDriverInfo );

    return 1;
}


int doAddDataSource()
{
    int bReturn = 1;

    switch ( cObjectSub )
    {
        case '\0':
            return doConfigDataSource( ODBC_ADD_DSN );
        case 'u':
            if ( !SQLSetConfigMode( (UWORD)ODBC_USER_DSN ) )
            {
                doPrintInstallerError( __FILE__, __LINE__ );
                fprintf( stderr, "[%s][%d][ERROR] Failed to set config mode.\n", __FILE__, __LINE__ );
                return 0;
            }
            bReturn = doConfigDataSource( ODBC_ADD_DSN );
            SQLSetConfigMode( (UWORD)ODBC_BOTH_DSN );
            break;
        case 's':
            if ( !SQLSetConfigMode( (UWORD)ODBC_SYSTEM_DSN ) )
            {
                doPrintInstallerError( __FILE__, __LINE__ );
                fprintf( stderr, "[%s][%d][ERROR] Failed to set config mode.\n", __FILE__, __LINE__ );
                return 0;
            }
            bReturn = doConfigDataSource( ODBC_ADD_DSN );
            SQLSetConfigMode( (UWORD)ODBC_BOTH_DSN );
            break;
        default:
            fprintf( stderr, "[%s][%d][ERROR] Missing or invalid object sub-type specified.\n", __FILE__, __LINE__ );
            return 0;
    }

    return bReturn;
}

int doEdit()
{
    switch ( cObject )
    {
        case 'd':
            return doEditDriver();
        case 's':
            return doEditDataSource();
        default:
            fprintf( stderr, "[%s][%d][ERROR] Missing or invalid object type specified.\n", __FILE__, __LINE__ );
            return 0;
    }
}

int doEditDriver()
{
    return 1;
}

int doEditDataSource()
{
    int bReturn = 1;

    switch ( cObjectSub )
    {
        case '\0':
            return doConfigDataSource( ODBC_CONFIG_DSN );
        case 'u':
            if ( !SQLSetConfigMode( ODBC_USER_DSN ) )
            {
                doPrintInstallerError( __FILE__, __LINE__ );
                fprintf( stderr, "[%s][%d][ERROR] Failed to set config mode.\n", __FILE__, __LINE__ );
                return 0;
            }
            bReturn = doConfigDataSource( ODBC_CONFIG_DSN );
            SQLSetConfigMode( ODBC_BOTH_DSN );
            break;
        case 's':
            if ( !SQLSetConfigMode( ODBC_SYSTEM_DSN ) )
            {
                doPrintInstallerError( __FILE__, __LINE__ );
                fprintf( stderr, "[%s][%d][ERROR] Failed to set config mode.\n", __FILE__, __LINE__ );
                return 0;
            }
            bReturn = doConfigDataSource( ODBC_CONFIG_DSN );
            SQLSetConfigMode( ODBC_BOTH_DSN );
            break;
        default:
            fprintf( stderr, "[%s][%d][ERROR] Invalid object sub-type specified.\n", __FILE__, __LINE__ );
            return 0;
    }

    return bReturn;
}

int doRemove()
{
    switch ( cObject )
    {
        case 'd':
            return doRemoveDriver();
        case 's':
            return doRemoveDataSource();
        default:
            fprintf( stderr, "[%s][%d][ERROR] Missing or invalid object type specified.\n", __FILE__, __LINE__ );
            return 0;
    }
}

/*!
  \brief  Deregister the driver.

      Simply removes driver from the list of known ODBC 
      drivers - does not remove any files.
*/
int doRemoveDriver()
{
    DWORD nUsageCount;
    BOOL  bRemoveDSNs = FALSE;

    if ( !pszName )
    {
        fprintf( stderr, "[%s][%d][ERROR] Please provide driver name.\n", __FILE__, __LINE__ );
        return 0;
    }

    if ( !SQLRemoveDriver( pszName, bRemoveDSNs, &nUsageCount ) )
    {
        doPrintInstallerError( __FILE__, __LINE__ );
        fprintf( stderr, "[%s][%d][ERROR] Failed to deregister driver.\n", __FILE__, __LINE__ );
        return 0;
    }

    printf( "[%s][%d][INFO] Driver deregistered. Usage count is %d.\n", __FILE__, __LINE__, (int)nUsageCount );
    return 1;
}

int doRemoveDataSource()
{
    int bReturn = 1;

    switch ( cObjectSub )
    {
        case '\0':
            bReturn = doRemoveDataSourceName();
            break;
        case 'u':
            if ( !SQLSetConfigMode( ODBC_USER_DSN ) )
            {
                doPrintInstallerError( __FILE__, __LINE__ );
                fprintf( stderr, "[%s][%d][ERROR] Failed to set config mode.\n", __FILE__, __LINE__ );
                return 0;
            }
            bReturn = doRemoveDataSourceName();
            SQLSetConfigMode( ODBC_BOTH_DSN );
        case 's':
            if ( !SQLSetConfigMode( ODBC_SYSTEM_DSN ) )
            {
                doPrintInstallerError( __FILE__, __LINE__ );
                fprintf( stderr, "[%s][%d][ERROR] Failed to set config mode.\n", __FILE__, __LINE__ );
                return 0;
            }
            bReturn = doRemoveDataSourceName();
            SQLSetConfigMode( ODBC_BOTH_DSN );
        default:
            fprintf( stderr, "[%s][%d][ERROR] Missing or invalid object sub-type specified.\n", __FILE__, __LINE__ );
            return 0;
    }

    return bReturn;
}

/*!
    \brief  Remove data source name from ODBC system information.
*/
int doRemoveDataSourceName()
{
    if ( SQLRemoveDSNFromIni( pszName ) == FALSE )
    {
        doPrintInstallerError( __FILE__, __LINE__ );
        fprintf( stderr, "[%s][%d][ERROR] Request failed.\n", __FILE__, __LINE__ );
        return 0;
    }

    return 1;
}

#if defined(_DIRECT_)
/*!
    \brief  Call drivers ConfigDSN when we are linked directly to driver.

    \note   This needs to be updated to reflect recent code changes.
*/
int doConfigDataSource( WORD nRequest )
{
    char  szAttributes[nDataSourceNameAttributesLength + strlen(pszDataSourceName) + 5];

    sprintf( szAttributes, "DSN=%s", pszDataSourceName );
    memcpy( &(szAttributes[strlen(szAttributes) + 1]), pszDataSourceNameAttributes, nDataSourceNameAttributesLength ); 
    if ( !ConfigDSN( (HWND)bGUI /* fake window handle */, nRequest, pszDriverName, szAttributes ) )
    {
        doPrintInstallerError( __FILE__, __LINE__ );
        return 0;
    }

    return 1;
}
#else
/*!
    \brief  Call drivers ConfigDSN without going through the DM.
*/
int doConfigDataSource( WORD nRequest )
{
    int                     bReturn             = FALSE; 
    MYODBCUTIL_DRIVER *     pDriver             = MYODBCUtilAllocDriver();
    MYODBCUTIL_DATASOURCE * pDataSourceGiven    = MYODBCUtilAllocDataSource( MYODBCUTIL_DATASOURCE_MODE_DSN_VIEW );
    MYODBCUTIL_DATASOURCE * pDataSource         = MYODBCUtilAllocDataSource( MYODBCUTIL_DATASOURCE_MODE_DSN_VIEW );
    BOOL                    (*pFunc)( HWND, WORD, LPCSTR, LPCSTR );
#if defined(WIN32)
    HINSTANCE               hLib                = 0;
#else
    void *                  hLib                = 0;
#endif
    char                    szDriver[1024];
    char                    szAttributes[1024];
    
    /* sanity check */
    if ( !pszAttributes )
    {
        fprintf( stderr, "[%s][%d][ERROR] Please provide an attributes string.\n", __FILE__, __LINE__ );
        goto doConfigDataSourceExit2;   
    }

    /* parse given attribute string */
    if ( !MYODBCUtilReadDataSourceStr( pDataSourceGiven, MYODBCUTIL_DELIM_SEMI, pszAttributes ) )
    {
        fprintf( stderr, "[%s][%d][ERROR] Malformed attribute string (%s)\n", __FILE__, __LINE__, pszAttributes );
        goto doConfigDataSourceExit2;
    }

    /* check that we have min attributes for request */
    if ( nRequest == ODBC_ADD_DSN )
    {
        if ( pDataSourceGiven->pszDRIVER )
        {
            /* pull driver from attributes as we can not have it in there for ConfigDSN() */
            sprintf( szDriver, "%s", pDataSourceGiven->pszDRIVER );
            free( pDataSourceGiven->pszDRIVER );
            pDataSourceGiven->pszDRIVER = NULL;
        }
        else
        {
            fprintf( stderr, "[%s][%d][WARNING] DRIVER attribute not provided - using %s.\n", __FILE__, __LINE__, MYODBCINST_DRIVER_NAME );
            sprintf( szDriver, "%s", MYODBCINST_DRIVER_NAME );
        }
    }
    else
    {
        if ( !pDataSourceGiven->pszDSN )
        {
            fprintf( stderr, "[%s][%d][ERROR] Please provide DSN attribute.\n", __FILE__, __LINE__ );
            goto doConfigDataSourceExit2;
        }

        /* get driver name for given dsn */
        if ( !MYODBCUtilReadDataSource( pDataSource, pDataSourceGiven->pszDSN ) )
        {
            fprintf( stderr, "[%s][%d][ERROR] Could not load data source name info (%s)\n", __FILE__, __LINE__, pDataSourceGiven->pszDSN );
            goto doConfigDataSourceExit2;
        }

        sprintf( szDriver, "%s", pDataSource->pszDRIVER );
    }

    /* get setup library name */
    if ( !MYODBCUtilReadDriver( pDriver, szDriver, NULL ) )
    {
        fprintf( stderr, "[%s][%d][ERROR] Could not load driver info (%s)\n", __FILE__, __LINE__, szDriver );
        goto doConfigDataSourceExit2;   
    }

    /* create a NULL delimited set of attributes for ConfigDSN() call */
    MYODBCUtilWriteDataSourceStr( pDataSourceGiven, MYODBCUTIL_DELIM_NULL, szAttributes, sizeof(szAttributes) - 1 );  

#if defined(WIN32)
    /* load it */
    hLib = LoadLibrary( pDriver->pszSETUP );
    if ( !hLib )
    {
        doPrintLastErrorString();
        fprintf( stderr, "[%s][%d][ERROR] Could not load driver setup library (%s).\n", __FILE__, __LINE__, pDriver->pszSETUP );
        goto doConfigDataSourceExit2;   
    }

    /* lookup ConfigDSN */
    pFunc = (BOOL (*)(HWND, WORD, LPCSTR, LPCSTR )) GetProcAddress( hLib, "ConfigDSN" );
    if ( !pFunc )
    {
        doPrintLastErrorString();
        fprintf( stderr, "[%s][%d][ERROR] Could not find ConfigDSN in (%s).\n", __FILE__, __LINE__, pDriver->pszSETUP );
        goto doConfigDataSourceExit1;
    }
#elif defined( __APPLE__ )

#else
    /* load it */
    lt_dlinit();
    if ( !(hLib = lt_dlopen( pDriver->pszSETUP )) )
    {
        fprintf( stderr, "[%s][%d][ERROR] Could not load driver setup library (%s). Error is %s\n", __FILE__, __LINE__, pDriver->pszSETUP, lt_dlerror() );
        goto doConfigDataSourceExit2;   
    }

    /* lookup ConfigDSN */
    pFunc = (BOOL (*)(HWND, WORD, LPCSTR, LPCSTR )) lt_dlsym( hLib, "ConfigDSN" );
    if ( !pFunc )
    {
        fprintf( stderr, "[%s][%d][ERROR] Could not find ConfigDSN in (%s). Error is %s\n", __FILE__, __LINE__, pDriver->pszSETUP, lt_dlerror() );
        goto doConfigDataSourceExit1;   
    }
#endif

    /* make call */
#if defined(__APPLE__)    
    if ( !AppleConfigDSN( (HWND)NULL, nRequest, szDriver, szAttributes ) )
#else
    /*!
        \note

        A fake window handle seems to work for platforms other than OSX :) It will not be
        used as a window handle - just as a flag to get the GUI.
    */    
    if ( !pFunc( (HWND)nWnd /* fake window handle */, nRequest, szDriver, szAttributes ) )
#endif
    {
        doPrintInstallerError( __FILE__, __LINE__ );
        goto doConfigDataSourceExit1;
    }

    bReturn = 1;

doConfigDataSourceExit1:
#if defined(WIN32)
    FreeLibrary( hLib );
#else
    lt_dlclose( hLib );
#endif

doConfigDataSourceExit2:
    MYODBCUtilFreeDataSource( pDataSource );
    MYODBCUtilFreeDataSource( pDataSourceGiven );
    MYODBCUtilFreeDriver( pDriver );

    return bReturn;
}
#endif



/* Some code added here as a hack to get build on osx 10.4 to work */

#ifdef __APPLE__
/*!
    \internal
    \brief      Adds a new DSN.

    \note       This function uses the current SQLSetConfigMode().
*/    
BOOL MYODBCSetupConfigDSNAdd( HWND hWnd, MYODBCUTIL_DATASOURCE *pDataSource )
{
    pDataSource->nMode = MYODBCUTIL_DATASOURCE_MODE_DSN_ADD;

    /*!
        ODBC RULE

        We must have a driver name.
    */    
    if ( !pDataSource->pszDRIVER )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_NAME, "Missing driver name." );
        return FALSE;
    }
    if ( !(*pDataSource->pszDRIVER) )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Missing driver name value." );
        return FALSE;
    }

    /*! 
        \todo 

        Use pDataSource->pszDriverFileName to get pDataSource->pszDRIVER
    */

    /*!
        ODBC RULE

        If a data source name is passed to ConfigDSN in lpszAttributes, ConfigDSN 
        checks that the name is valid.
    */    
    if ( pDataSource->pszDSN )
    {
        /*!
            ODBC RULE

            ConfigDSN should call SQLValidDSN to check the length of the data source 
            name and to verify that no invalid characters are included in the name.
        */    
        /*!
            MYODBC RULE
             
            Assumption is that this also checks to ensure we are not trying to create 
            a DSN using an odbc.ini reserved section name. 
        */
        if ( !SQLValidDSN( pDataSource->pszDSN ) )
        {
            SQLPostInstallerError( ODBC_ERROR_REQUEST_FAILED, "DSN contains illegal characters or length does not make sense." );
            return FALSE;
        }
    }

    /*!
        ODBC RULE

        If lpszAttributes contains enough information to connect to a data source, 
        ConfigDSN can add the data source or display a dialog box with which the user 
        can change the connection information. If lpszAttributes does not contain 
        enough information to connect to a data source, ConfigDSN must determine the 
        necessary information; if hwndParent is not null, it displays a dialog box to 
        retrieve the information from the user.
    */

    /*!
        ODBC RULE
        
        If ConfigDSN cannot get complete connection information for a data source, it 
        returns FALSE.
    */
    /*!
        MYODBC RULE

        We want pszDriver and a DSN attribute - we can default the rest.
    */    
    if ( !pDataSource->pszDSN )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Missing DSN attribute." );
        return FALSE;
    }

    if ( !(*pDataSource->pszDSN) )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Missing DSN attribute value." );
        return FALSE;
    }

    /*!
        ODBC RULE

        If the data source name matches an existing data source name and hwndParent is null, 
        ConfigDSN overwrites the existing name. If it matches an existing name and hwndParent 
        is not null, ConfigDSN prompts the user to overwrite the existing name.        
    */
    return MYODBCUtilWriteDataSource( pDataSource );
}

/*!
    \internal
    \brief      Configure an existing DSN.

    \note       This function uses the current SQLSetConfigMode().
*/    
BOOL MYODBCSetupConfigDSNEdit( HWND hWnd, MYODBCUTIL_DATASOURCE *pDataSource )
{
    pDataSource->nMode = MYODBCUTIL_DATASOURCE_MODE_DSN_EDIT;

    /*!
        ODBC RULE

        To modify a data source, a data source name must be passed to ConfigDSN in 
        lpszAttributes.
    */    
    if ( !pDataSource->pszDSN )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Missing DSN attribute." );
        return FALSE;
    }

    if ( !(*pDataSource->pszDSN) )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Missing DSN attribute value." );
        return FALSE;
    }

    /*!
        ODBC RULE

        ConfigDSN should call SQLValidDSN to check the length of the data source 
        name and to verify that no invalid characters are included in the name.
    */
    /*!
        MYODBC RULE
         
        Assumption is that this also checks to ensure we are not trying to create 
        a DSN using an odbc.ini reserved section name. 
    */
    if ( !SQLValidDSN( pDataSource->pszDSN ) )
    {
        SQLPostInstallerError( ODBC_ERROR_REQUEST_FAILED, "DSN contains illegal characters or length does not make sense." );
        return FALSE;
    }

    /*!
        ODBC RULE

        ConfigDSN checks that the data source name is in the Odbc.ini file (or 
        registry).
    */
    if ( !MYODBCUtilDSNExists( pDataSource->pszDSN ) )
    {
        SQLPostInstallerError( ODBC_ERROR_REQUEST_FAILED, "DSN does not exist." );
        return FALSE;
    }

    /* merge in any missing attributes we can find in the system information */
    MYODBCUtilReadDataSource( pDataSource, pDataSource->pszDSN );

    /*!
        ODBC RULE

        If the data source name was not changed, ConfigDSN calls 
        SQLWritePrivateProfileString in the installer DLL to make any other changes.
    */
    /*!
        MYODBC RULE

        We do not support changing the DSN name.
    */
    return MYODBCUtilWriteDataSource( pDataSource );
}

/*!
    \internal
    \brief      Remove given DSN.

    \note       This function uses the current SQLSetConfigMode().
*/    
BOOL MYODBCSetupConfigDSNRemove( MYODBCUTIL_DATASOURCE *pDataSource )
{
    /*!
        ODBC RULE

        To delete a data source, a data source name must be passed to ConfigDSN 
        in lpszAttributes.
    */    
    if ( !pDataSource->pszDSN )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Missing DSN attribute." );
        return FALSE;
    }

    if ( !(*pDataSource->pszDSN) )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Missing DSN attribute value." );
        return FALSE;
    }

    /*!
        ODBC RULE

        ConfigDSN should call SQLValidDSN to check the length of the data source 
        name and to verify that no invalid characters are included in the name.
    */    
    /*!
        MYODBC RULE

        Assumption is that this also checks to ensure we are not trying to create 
        a DSN using an odbc.ini reserved section name. 
    */
    if ( !SQLValidDSN( pDataSource->pszDSN ) )
    {
        SQLPostInstallerError( ODBC_ERROR_REQUEST_FAILED, "DSN contains illegal characters or length does not make sense." );
        return FALSE;
    }

    /*!
        ODBC RULE

        ConfigDSN checks that the data source name is in the Odbc.ini file (or 
        registry).
    */    
    if ( !MYODBCUtilDSNExists( pDataSource->pszDSN ) )
    {
        SQLPostInstallerError( ODBC_ERROR_REQUEST_FAILED, "DSN does not exist." );
        return FALSE;
    }

    /*!
        ODBC RULE

        It then calls SQLRemoveDSNFromIni in the installer DLL to remove the 
        data source.
    */    
    return SQLRemoveDSNFromIni( pDataSource->pszDSN );
}


/*!
    \brief  Add, edit, or remove a Data Source Name (DSN).

            This function should be called from the ODBC Administrator
            program when our driver is being used during a request to
            add, edit or remove a DSN. This allows us to do driver 
            specific stuff such as use our dialogs to work with our
            driver.

            This function is also a viable entry point and a public API
            for use by special function code such as an installer or an
            application which has embedded the driver functionality.
*/  
BOOL INSTAPI AppleConfigDSN( HWND hWnd, WORD nRequest, LPCSTR pszDriver, LPCSTR pszAttributes )
{
    MYODBCUTIL_DATASOURCE * pDataSource = MYODBCUtilAllocDataSource( MYODBCUTIL_DATASOURCE_MODE_DSN_VIEW );
    BOOL                    bReturn     = FALSE;

    /*
        \note   unixODBC
    
                In some cases on unixODBC a semi-colon will be used
                to indicate the end of name/value pair. This is like
                in SQLDriverConnect(). This is incorrect but we try
                to simply ignore semi-colon and hope rest of format
                is ok.

                So we should call this with MYODBCUTIL_DELIM_NULL but we use
                MYODBCUTIL_DELIM_BOTH instead.

                This was tested with pszAttributes "DSN=test;".    
    */
    if ( !MYODBCUtilReadDataSourceStr( pDataSource, MYODBCUTIL_DELIM_BOTH, pszAttributes ) )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Data Source string seems invalid." );
        goto exitConfigDSN;
    }

    /*!
        ODBC RULE

        DRIVER is not a valid attribute for ConfigDSN().
        Also; ConfigDSN may not delete or change the value of the Driver keyword...
        when ODBC_CONFIG_DSN.
    */
    if ( pDataSource->pszDRIVER )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "DRIVER is an invalid attribute." );
        goto exitConfigDSN;
    }

    /*!
        ODBC RULE

        Driver description (usually the name of the associated DBMS) presented to users 
        instead of the physical driver name.
    */    
    if ( !pszDriver || !(*pszDriver) )
    {
        SQLPostInstallerError( ODBC_ERROR_INVALID_KEYWORD_VALUE, "Need driver name." );
        goto exitConfigDSN;
    }

    pDataSource->pszDRIVER = (char *)strdup( pszDriver );

    switch ( nRequest )
    {
        case ODBC_ADD_DSN:
            bReturn = MYODBCSetupConfigDSNAdd( hWnd, pDataSource );
            break;
        case ODBC_CONFIG_DSN:
            bReturn = MYODBCSetupConfigDSNEdit( hWnd, pDataSource );
            break;
        case ODBC_REMOVE_DSN:
            bReturn = MYODBCSetupConfigDSNRemove( pDataSource );
            break;
        default:
            SQLPostInstallerError( ODBC_ERROR_INVALID_REQUEST_TYPE, "Invalid request." );
    }

exitConfigDSN:
    MYODBCUtilFreeDataSource( pDataSource );
    return bReturn;
}     

#endif

