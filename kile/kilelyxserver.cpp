/***************************************************************************
    begin                : Sat Sept 9 2003
    edit		 : Tue Mar 20 2007
    copyright            : (C) 2003 by Jeroen Wijnhout, 2007 by Thomas Braun
    email                : Jeroen.Wijnhout@kdemail.net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h> //getenv
#include <unistd.h> //read
#include <fcntl.h>

#include "kilelyxserver.h"
#include "kileactions.h"

#include <qfile.h>
#include <qfileinfo.h>
#include <qsocketnotifier.h>
#include <qregexp.h>
#include <qdir.h>

#include <kdebug.h>
#include <klocale.h>

KileLyxServer::KileLyxServer(bool startMe) :
	m_perms( S_IRUSR | S_IWUSR ),m_running(false)
{	
	kdDebug() << "===KileLyxServer::KileLyxServer(bool" << startMe << ")===" << endl;
	m_pipeIn.setAutoDelete(true);
	m_notifier.setAutoDelete(true);

	m_file.setAutoDelete(false);
	m_tempDir = new KTempDir();
	if(!m_tempDir)
		return;

	m_tempDir->setAutoDelete(true);

	m_links << ".lyxpipe.in" << ".lyx/lyxpipe.in";
	m_links << ".lyxpipe.out" << ".lyx/lyxpipe.out";

	for(uint i = 0; i< m_links.count() ; i++)
	{
		m_pipes.append( m_tempDir->name() + m_links[i] );
		m_links[i].prepend(QDir::homeDirPath() + "/" );
		kdDebug() << "m_pipes[" << i << "]=" << m_pipes[i] << endl;
		kdDebug() << "m_links[" << i << "]=" << m_links[i] << endl;
	}

	if (startMe)
		start();
}

KileLyxServer::~KileLyxServer()
{
	stop();
	removePipes();
	delete m_tempDir;
}

bool KileLyxServer::start()
{
	if (m_running)
		stop();

	kdDebug() << "Starting the LyX server..." << endl;

	if (openPipes())
	{
		QSocketNotifier *notifier;
		QPtrListIterator<QFile> it(m_pipeIn);
		while (it.current())
		{
			if ((*it)->name().right(3) == ".in" )
			{
				notifier = new QSocketNotifier((*it)->handle(), QSocketNotifier::Read, this);
				connect(notifier, SIGNAL(activated(int)), this, SLOT(receive(int)));
				m_notifier.append(notifier);
				kdDebug() << "Created notifier for " << (*it)->name() << endl;
			}
			else
				kdDebug() << "No notifier created for " << (*it)->name() << endl;
			++it;
		}
		m_running=true;
	}

	return m_running;
}

bool KileLyxServer::openPipes()
{	
	kdDebug() << "===bool KileLyxServer::openPipes()===" << endl;
	
	bool opened = false;
	QFileInfo pipeInfo,linkInfo;
	QFile *file;
	struct stat buf;
	struct stat *stats = &buf;
	
	for (uint i=0; i < m_pipes.count(); ++i)
	{
		pipeInfo.setFile(m_pipes[i]);
		linkInfo.setFile(m_links[i]);
 		
		QFile::remove(linkInfo.absFilePath());
		linkInfo.refresh();
 		
		if ( !pipeInfo.exists() )
		{
			//create the dir first
			if ( !QFileInfo(pipeInfo.dirPath(true)).exists() )
				if ( mkdir(QFile::encodeName( pipeInfo.dirPath() ), m_perms | S_IXUSR) == -1 )
				{
					kdError() << "Could not create directory for pipe" << endl;
					continue;
				}
				else
					kdDebug() << "Created directory " << pipeInfo.dirPath() << endl;

				if ( mkfifo(QFile::encodeName( pipeInfo.absFilePath() ), m_perms) != 0 )
				{
					kdError() << "Could not create pipe: " << pipeInfo.absFilePath() << endl;
					continue;				
				}
				else
					kdDebug() << "Created pipe: " << pipeInfo.absFilePath() << endl;
		}
		
		if ( symlink(QFile::encodeName(pipeInfo.absFilePath()),QFile::encodeName(linkInfo.absFilePath())) != 0 )
		{
			kdError() << "Could not create symlink: " << linkInfo.absFilePath() << " --> " << pipeInfo.absFilePath() << endl;
			continue;
		}

		file  = new QFile(pipeInfo.absFilePath());
		pipeInfo.refresh();

		if( pipeInfo.exists() && file->open(IO_ReadWrite) ) // in that order we don't create the file if it does not exist
		{
			kdDebug() << "Opened file: " << pipeInfo.absFilePath() << endl;
			fstat(file->handle(),stats);
			if( !S_ISFIFO(stats->st_mode) )
			{
				kdError() << "The file " << pipeInfo.absFilePath() <<  "we just created is not a pipe!" << endl;
				file->close();
				continue;
			}
			else
			{	// everything is correct :)
				m_pipeIn.append(file);
				m_file.insert(file->handle(),file);
				opened=true;
			}
		}
		else
			kdError() << "Could not open " << pipeInfo.absFilePath() << endl;
	}
	return opened;
}

void KileLyxServer::stop()
{
	kdDebug() << "Stopping the LyX server..." << endl;

	QPtrListIterator<QFile> it(m_pipeIn);
	while (it.current())
	{
		(*it)->close();
		++it;
	}

	m_pipeIn.clear();
	m_notifier.clear();

	m_running=false;
}

void KileLyxServer::removePipes()
{
  	for ( uint i = 0; i < m_links.count(); ++i)
 		QFile::remove(m_links[i]);
 	for ( uint i = 0; i < m_pipes.count(); ++i)
		QFile::remove(m_pipes[i]);

}

void KileLyxServer::processLine(const QString &line)
{
	kdDebug() << "===void KileLyxServer::processLine(const QString " << line << ")===" << endl;
	
	QRegExp cite(":citation-insert:(.*)$");
	QRegExp bibtexdbadd(":bibtex-database-add:(.*)$");

	if (cite.search(line) > -1)
		emit(insert(KileAction::TagData("Cite", "\\cite{"+cite.cap(1)+'}', QString::null, 7+cite.cap(1).length())));
	else if ( bibtexdbadd.search(line) > -1 )
		emit(insert(KileAction::TagData("BibTeX db add", "\\bibliography{"+ bibtexdbadd.cap(1) + '}', QString::null, 15+bibtexdbadd.cap(1).length())));
}

void KileLyxServer::receive(int fd)
{
 	if (m_file[fd])
 	{
 		int bytesRead;
 		int const size = 256;
        char buffer[size];
 		if ((bytesRead = read(fd, buffer, size - 1)) > 0 )
 		{
  			buffer[bytesRead] = '\0'; // turn it into a c string
            		QStringList cmds = QStringList::split('\n', QString(buffer).stripWhiteSpace());
			for ( uint i = 0; i < cmds.count(); ++i )
				processLine(cmds[i]);
		}
 	}
}

#include "kilelyxserver.moc"
