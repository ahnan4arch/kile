/***************************************************************************
                          kilelauncher.cpp  -  description
                             -------------------
    begin                : mon 3-11 20:40:00 CEST 2003
    copyright            : (C) 2003 by Jeroen Wijnhout
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

#include "kiletool.h"
#include "kiletoolmanager.h"
#include "kiletool_enums.h"
#include "kilelauncher.h"
#include "docpart.h"

#include <qwidgetstack.h>
#include <qregexp.h>

#include <kdebug.h>
#include <kprocess.h>
#include <klocale.h>
#include <klibloader.h>
#include <kparts/part.h>
#include <kparts/partmanager.h>

 namespace KileTool
{
	Launcher::Launcher() :
		m_tool(0L)
	{
	}
	
	Launcher::~ Launcher()
	{
		kdDebug() << "DELETING launcher" << endl;
	}
	
	void Launcher::translate(QString &str)
	{
		QDictIterator<QString> it(*paramDict());
		for( it.toFirst() ; it.current(); ++it )
		{
			kdDebug() << "translate: " << str << " key " << it.currentKey() << " value " << *(it.current()) << endl;
			str.replace(it.currentKey(), *( it.current() ) );
		}
	}

	ProcessLauncher::ProcessLauncher(const char * shellname /* =0 */) :
		m_wd(QString::null),
		m_changeTo(true)
	{
		kdDebug() << "==KileTool::ProcessLauncher::ProcessLauncher()==============" << endl;
		m_proc = new KShellProcess(shellname);
		if (m_proc)
			kdDebug() << "\tKShellProcess created" << endl;
		else
			kdDebug() << "\tNO KShellProcess created" << endl;

		connect(m_proc, SIGNAL( receivedStdout(KProcess*, char*, int) ), this, SLOT(slotProcessOutput(KProcess*, char*, int ) ) );
 		connect(m_proc, SIGNAL( receivedStderr(KProcess*, char*, int) ),this, SLOT(slotProcessOutput(KProcess*, char*, int ) ) );
		connect(m_proc, SIGNAL( processExited(KProcess*)), this, SLOT(slotProcessExited(KProcess*)));
	}

	ProcessLauncher::~ProcessLauncher()
	{
		kdDebug() << "DELETING ProcessLauncher" << endl;
		delete m_proc;
	}

	void ProcessLauncher::setWorkingDirectory(const QString &wd)
	{
		m_wd = wd;
	}

	bool ProcessLauncher::launch()
	{
		kdDebug() << "KileTool::ProcessLauncher::launch()=================" << endl;
		kdDebug() << "\tbelongs to tool" << tool()->name() << endl;

		QString msg, out = "*****\n*****     " + tool()->name() + i18n(" output: \n");

		m_cmd = tool()->readEntry("command");
		QString opt = tool()->readEntry("options");
		
		if ( m_changeTo && (m_wd != QString::null ) )
		{
			m_proc->setWorkingDirectory(m_wd);
			out += QString("*****     cd '")+ m_wd +QString("'\n");
		}

		QString str;
		translate(m_cmd);
		translate(opt);
        	*m_proc  << m_cmd << opt;
		
		if (m_proc)
		{
			
			out += QString("*****     ")+ m_cmd+ " " +opt +QString("\n");


			QString src = tool()->source(false);
			QString trgt = tool()->target();
			if (src == trgt)
				msg = src;
			else
				msg = src + " => " + trgt;

			msg += " ("+m_proc->args()[0]+")";
			 
			emit(message(Info,msg));

			out += "*****\n";
			emit(output(out));

			bool r = m_proc->start(KProcess::NotifyOnExit, KProcess::AllOutput);
			if (r) kdDebug() << "launch successful" << endl;
			else kdDebug() << "launch failed" << endl;
			return r;
		}
		else
			return false;
	}

	bool ProcessLauncher::kill()
	{
		kdDebug() << "==KileTool::ProcessLauncher::kill()==============" << endl;
		if ( m_proc && m_proc->isRunning() )
		{
			kdDebug() << "\tkilling" << endl;
			return m_proc->kill();
		}
		else
		{
			kdDebug() << "\tno process or process not running" << endl;
			return false;
		}
	}

	void ProcessLauncher::slotProcessOutput(KProcess*, char* buf, int len)
	{
		QString s = QCString(buf, len);
		emit(output(s));
	}

	void ProcessLauncher::slotProcessExited(KProcess*)
	{
		kdDebug() << "==KileTool::ProcessLauncher::slotProcessExited=============" << endl;
		kdDebug() << "\t" << tool()->name() << endl;

		if (m_proc)
		{
			if (m_proc->normalExit())
			{
				kdDebug() << "\tnormal exit" << endl;
				int type = Info;
				if (m_proc->exitStatus() != 0) 
				{
					type = Error;
					emit(message(type,i18n("finished with exit status %1").arg(m_proc->exitStatus())));
				}

				if (type == Info)
					emit(done(Success));
				else
					emit(done(Failed));
			}
			else
			{
				kdDebug() << "\tabnormal exit" << endl;
				emit(message(Error,i18n("finished abruptedly")));
				//emit(abnormalExit());
				emit(done(AbnormalExit));
			}
		}
		else
		{
			kdWarning() << "\tNO PROCESS, emitting done" << endl;
			emit(done(Success));
		}
	}

	PartLauncher::PartLauncher(const char *name /* = 0*/ ) :
		m_part(0L),
		m_state("Viewer"),
		m_name(name),
		m_libName(0L),
		m_className(0L),
		m_options(QString::null)
	{
	}

	PartLauncher::~PartLauncher()
	{
	}

	bool PartLauncher::launch()
	{

		m_libName = tool()->readEntry("libName").ascii();
		m_className = tool()->readEntry("className").ascii();
		m_options=tool()->readEntry("libOptions");
		m_state=tool()->readEntry("state");

		QString msg, out = "*****\n*****     " + tool()->name() + i18n(" output: \n");
		
		QString shrt = "%target";
		translate(shrt);
		QString dir  = "%dir_target"; translate(dir);

		QString name = shrt;
		if ( dir[0] == '/' )
			name = dir + "/" + shrt;


		KLibFactory *factory = KLibLoader::self()->factory(m_libName);
		if (factory == 0)
		{
			emit(message(Error, i18n("Couldn't find the %1 library! %2 is not started.").arg(m_libName).arg(tool()->name())));
			return false;
		}

		QWidgetStack *stack = tool()->manager()->widgetStack();
		KParts::PartManager *pm = tool()->manager()->partManager();

		m_part = (KParts::ReadOnlyPart *)factory->create(stack, m_libName, m_className, m_options);

		if (m_part == 0)
		{
			emit(message(Error, i18n("Couldn't start %1! %2 is not started.").arg(QString(m_name)+" "+m_options).arg(m_name)));
			emit(done(Failed));
			return false;
		}
		else
		{
			QString cmd = QString(m_libName)+"->"+QString(m_className)+" "+m_options+" "+name;
			out += "*****     " + cmd + "\n";

			msg = shrt+ " (" + tool()->readEntry("libName") + ")";
			emit(message(Info,msg));
		}

		out += "*****\n";
		emit(output(out));

		tool()->manager()->wantGUIState(m_state);

		stack->addWidget(m_part->widget() , 1 );
		stack->raiseWidget(1);

		m_part->openURL(name);
		pm->addPart(m_part, true);
		pm->setActivePart(m_part);

		emit(done(Success));

		return true;
	}

	bool PartLauncher::kill()
	{
		return true;
	}

	bool DocPartLauncher::launch()
	{
		m_state=tool()->readEntry("state");

		QString shrt = "%target";
		translate(shrt);
		QString name="%dir_target/%target";
		translate(name);

		QWidgetStack *stack = tool()->manager()->widgetStack();
		KParts::PartManager *pm = tool()->manager()->partManager();

		docpart *htmlpart = new docpart(stack,"help");
		m_part = static_cast<KParts::ReadOnlyPart*>(htmlpart);

		connect(htmlpart, SIGNAL(updateStatus(bool, bool)), tool(), SIGNAL(updateStatus(bool, bool)));

		tool()->manager()->wantGUIState(m_state);

		htmlpart->openURL(name);
		htmlpart->addToHistory(name);
		stack->addWidget(htmlpart->widget() , 1 );
		stack->raiseWidget(1);

		pm->addPart(htmlpart, true);
		pm->setActivePart( htmlpart);

		emit(done(Success));
		
		return true;
	}
}

#include "kilelauncher.moc"
