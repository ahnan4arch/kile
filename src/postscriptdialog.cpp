/***************************************************************************
    date                 : Mar 12 2007
    version              : 0.20
    copyright            : (C) 2005-2007 by Holger Danielsson
    email                : holger.danielsson@versanet.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// 2007-03-12 dani
//  - use KileDocument::Extensions

#include "postscriptdialog.h"

#include <QLabel>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLayout>
#include <QProcess>
#include <QSpinBox>
#include <QStringList>

#include <KComboBox>
#include <KFileDialog>
#include <KIconLoader>
#include <KLineEdit>
#include <KLocale>
#include <KMessageBox>
#include <KProcess>
#include <KStandardDirs>
#include <KTemporaryFile>
#include <KUrlRequester>

#include "kiledebug.h"
#include "kiletool_enums.h"

namespace KileDialog
{

PostscriptDialog::PostscriptDialog(QWidget *parent,
                                   const QString &texfilename,const QString &startdir,
                                   const QString &latexextensions,
                                   KileWidget::LogWidget *log,KileWidget::Output *output) :
	KDialog(parent),
	m_startdir(startdir),
	m_log(log),
	m_output(output),
	m_proc(0)
{	
	setCaption(i18n("Rearrange Postscript File"));
	setModal(true);
	setButtons(Close | User1);
	setDefaultButton(User1);
	showButtonSeparator(true);

	// determine if a psfile already exists
	QString psfilename;
	if ( !texfilename.isEmpty() ) {
		// working with a postscript document, so we try to determine the LaTeX source file
		QStringList extlist = latexextensions.split(" ");
		for (QStringList::Iterator it = extlist.begin(); it != extlist.end(); ++it) {
			if (texfilename.indexOf((*it), -(*it).length()) >= 0) {
				psfilename = texfilename.left(texfilename.length() - (*it).length()) + ".ps";
				if (!QFileInfo(psfilename).exists())
					psfilename = QString::null;
				break;
			}
		}
	}

	// prepare dialog
	QWidget *page = new QWidget(this);
	setMainWidget(page);
	m_PostscriptDialog.setupUi(page);

	// line 0: QLabel
	bool pstops = !KStandardDirs::findExe("pstops").isNull();
	bool psselect = !KStandardDirs::findExe("psselect").isNull();

	if (!pstops || !psselect) {
		QString msg = QString::null;
		if (!pstops) { 
			msg = "'pstops'";
			if (!psselect)
				msg += " and ";
		}
		if (!psselect) {
			msg += "'psselect'";
		}
		m_PostscriptDialog.m_lbInfo->setText(m_PostscriptDialog.m_lbInfo->text() + "\n(Error: " + msg + " not found.)");
	}

	m_PostscriptDialog.m_edInfile->lineEdit()->setText(psfilename);

	if (pstops) {
		m_PostscriptDialog.m_cbTask->insertItem(i18n("1 DIN A5 Page + Empty Page --> DIN A4")); // 0   PS_A5_EMPTY
		m_PostscriptDialog.m_cbTask->insertItem(i18n("1 DIN A5 Page + Duplicate --> DIN A4"));  // 1   PS_A5_DUPLICATE
		m_PostscriptDialog.m_cbTask->insertItem(i18n("2 DIN A5 Pages --> DIN A4"));             // 2   PS_2xA5
		m_PostscriptDialog.m_cbTask->insertItem(i18n("2 DIN A5L Pages --> DIN A4"));            // 3   PS_2xA5L
		m_PostscriptDialog.m_cbTask->insertItem(i18n("4 DIN A5 Pages --> DIN A4"));             // 4   PS_4xA5
		m_PostscriptDialog.m_cbTask->insertItem(i18n("1 DIN A4 Page + Empty Page --> DIN A4")); // 5   m_PostscriptDialog.PS_A4_EMPTY
		m_PostscriptDialog.m_cbTask->insertItem(i18n("1 DIN A4 Page + Duplicate --> DIN A4"));  // 6   PS_A4_DUPLICATE
		m_PostscriptDialog.m_cbTask->insertItem(i18n("2 DIN A4 Pages --> DIN A4"));             // 7   PS_2xA4
		m_PostscriptDialog.m_cbTask->insertItem(i18n("2 DIN A4L Pages --> DIN A4"));            // 8   PS_2xA4L
	}
	if (psselect) {
		m_PostscriptDialog.m_cbTask->insertItem(i18n("Select Even Pages"));                 // 9   PS_EVEN
		m_PostscriptDialog.m_cbTask->insertItem(i18n("Select Odd Pages"));                  // 10  PS_ODD
		m_PostscriptDialog.m_cbTask->insertItem(i18n("Select Even Pages (reverse order)")); // 11  m_PostscriptDialog.PS_EVEN_REV
		m_PostscriptDialog.m_cbTask->insertItem(i18n("Select Odd Pages (reverse order)"));  // 12  PS_ODD_REV
		m_PostscriptDialog.m_cbTask->insertItem(i18n("Reverse All Pages"));                 // 13  PS_REVERSE
		m_PostscriptDialog.m_cbTask->insertItem(i18n("Copy All Pages (sorted)"));           // 14  PS_COPY_SORTED
	}
	if (pstops) {
		m_PostscriptDialog.m_cbTask->insertItem(i18n("Copy All Pages (unsorted)")); // 15  PS_COPY_UNSORTED
		m_PostscriptDialog.m_cbTask->insertItem(i18n("pstops: Choose Parameter"));  // 16  PS_PSTOPS_FREE 
	}
	if (psselect) {
		m_PostscriptDialog.m_cbTask->insertItem(i18n("psselect: Choose Parameter")); // 17  PS_PSSELECT_FREE 
	}

	m_PostscriptDialog.m_edInfile->setFilter("*.ps|PS Files\n*.ps.gz|Zipped PS Files");
	m_PostscriptDialog.m_edOutfile->setFilter("*.ps|PS Files\n*.ps.gz|Zipped PS Files");

	// choose one common task
	m_PostscriptDialog.m_cbTask->setCurrentIndex(PS_2xA4);
	comboboxChanged(PS_2xA4);

	// set an user button to execute the task
	setButtonText(Close, i18n("Done"));
	setButtonText(User1, i18n("Execute"));
	setButtonIcon(User1, KIcon("system-run"));
	if (!pstops && !psselect)
		enableButton(User1, false);

	// some connections
	connect(m_PostscriptDialog.m_cbTask, SIGNAL(activated(int)), this, SLOT(comboboxChanged(int)));
	connect(this, SIGNAL(output(const QString &)), m_output, SLOT(receive(const QString &)));

	setFocusProxy(m_PostscriptDialog.m_edInfile);
	m_PostscriptDialog.m_edInfile->setFocus();
}

PostscriptDialog::~PostscriptDialog()
{
	if (m_proc)
		delete m_proc;
}

void PostscriptDialog::slotButtonClicked(int button)
{
	if (button == User1) {
		if (checkParameter()) {
			execute();
		}
	}
	KDialog::slotButtonClicked(button);
}

void PostscriptDialog::execute()
{
	m_tempfile = buildTempfile();
	if ( m_tempfile != QString::null ) {
		m_log->clear();
		QFileInfo from(m_PostscriptDialog.m_edInfile->lineEdit()->text());
		QFileInfo to(m_PostscriptDialog.m_edOutfile->lineEdit()->text());

		// output for log window
		QString msg = i18n("rearrange ps file: ") + from.fileName();
		if (!to.fileName().isEmpty())
			msg += " ---> " + to.fileName();
		m_log->printMsg(KileTool::Info, msg, m_program);

		// some output logs 
		m_output->clear();
		QString s = QString("*****\n")
		              + i18n("***** tool:        ") + m_program + ' ' + m_param + '\n'
		              + i18n("***** input file:  ") + from.fileName()+ '\n'
		              + i18n("***** output file: ") + to.fileName()+ '\n'
		              + i18n("***** viewer:      ") + ((m_PostscriptDialog.m_cbView->isChecked()) ? i18n("yes") : i18n("no")) + '\n'
		              + "*****\n";
		emit( output(s) );
 
		// delete old KProcess
		if (m_proc)
			delete m_proc;

		m_proc = new KProcess();
		m_proc->setShellCommand("sh " + m_tempfile);
		m_proc->setOutputChannelMode(KProcess::MergedChannels);
		m_proc->setReadChannel(QProcess::StandardOutput);

		connect(m_proc, SIGNAL(readyReadStandardOutput()),
		        this, SLOT(slotProcessOutput()));
		connect(m_proc, SIGNAL(readyReadStandardError()),
		        this, SLOT(slotProcessOutput()));
		connect(m_proc, SIGNAL(finished(int, QProcess::ExitStatus)),
		        this, SLOT(slotProcessExited(int, QProcess::ExitStatus)));

		KILE_DEBUG() << "=== PostscriptDialog::runPsutils() ====================";
		KILE_DEBUG() << "   execute '" << m_tempfile << "'";
		m_proc->start();
	}
	
}

void PostscriptDialog::slotProcessOutput()
{
	emit(output(m_proc->readAllStandardOutput()));
	emit(output(m_proc->readAllStandardError()));
}


void PostscriptDialog::slotProcessExited(int exitCode, QProcess::ExitStatus exitStatus)
{
	if (exitStatus != QProcess::NormalExit)
		showError(i18n("An error occurred, while rearranging the file."));

	QFile::remove(m_tempfile);
}

#ifdef __GNUC__
#warning FIXME: redesign the method buildTempfile(). It won't work correctly like it is now!
#endif
QString PostscriptDialog::buildTempfile()
{
	// build command
	m_program = "pstops";          // default
	m_param = "";

	switch (m_PostscriptDialog.m_cbTask->currentItem()) {
		case PS_A5_EMPTY:      m_param = "1:0L(29.7cm,0cm)";
		                       break;
		case PS_A5_DUPLICATE:  m_param = "1:0L(29.7cm,0cm)+0L(29.7cm,14.85cm)";
		                       break;
		case PS_2xA5:          m_param = "2:0L(29.7cm,0cm)+1L(29.7cm,14.85cm)";
		                       break;
		case PS_2xA5L:         break;
		case PS_4xA5:          m_param = "4:0@0.7(0cm,8.7cm)"
		                                 "+1@0.7(10.5cm,8.7cm)"
		                                 "+2@0.7(0cm,-6.15cm)"
		                                 "+3@0.7(10.5cm,-6.15cm)";
		                       break;
		case PS_A4_EMPTY:      m_param = "1:0L@0.7(21cm,0cm)";
		                       break;
		case PS_A4_DUPLICATE:  m_param = "1:0L@0.7(21cm,0cm)+0L@0.7(21cm,14.85cm)";
		                       break;
		case PS_2xA4:          m_param = "2:0L@0.7(21cm,0cm)+1L@0.7(21cm,14.85cm)";
		                       break;
		case PS_2xA4L:         m_param = "2:0R@0.7(0cm,29.7cm)+1R@0.7(0cm,14.85cm)";
		                       break;
		case PS_EVEN:          m_program = "psselect";
		                       m_param = "-e";
		                       break;
		case PS_ODD:           m_program = "psselect";
		                       m_param = "-o";
		                       break;
		case PS_EVEN_REV:      m_program = "psselect";
		                       m_param = "-e -r";
		                       break;
		case PS_ODD_REV:       m_program = "psselect";
		                       m_param = "-o -r";
		                       break;
		case PS_REVERSE:       m_program = "psselect";
		                       m_param = "-r";
		                       break;
		case PS_COPY_SORTED:   m_program = "psselect";
		                       m_param = "-p" + duplicateParameter("1-");
		                       break;
		case PS_COPY_UNSORTED: m_param = "1:" + duplicateParameter("0");
		                       break;
		case PS_PSTOPS_FREE:   m_param = m_PostscriptDialog.m_edParameter->text();
		                       break;
		case PS_PSSELECT_FREE: m_program = "psselect";
		                       m_param = m_PostscriptDialog.m_edParameter->text();
		                       break;
	}

	// create a temporary file
	KTemporaryFile temp;
	temp.setSuffix(".sh");
	temp.setAutoRemove(false);
	if(!temp.open()) {
#ifdef __GNUC__
#warning FIXME: add error handling
#endif
	}
	QString tempname = temp.name();
	
	QTextStream stream(&temp);
	stream << "#! /bin/sh" << endl;

	// accept only ".ps" or ".ps.gz" as an input file
	QFileInfo fi(m_PostscriptDialog.m_edInfile->lineEdit()->text());
	bool zipped_psfile = (fi.completeSuffix() == "ps.gz") ? true : false;

	// there are four possible cases
	//         outfile view
	//     1)    +      +        pstops/psselect + okular
	//     2)    +      -        pstops/psselect
	//     3)    -      +        pstops/psselect | okular (nur Shell)
	//     4)    -      -        error (already detected by checkParameter())

	// some files, which are used
	QString command    = m_program + " \"" + m_param + "\"";
	QString inputfile  = "\"" + m_PostscriptDialog.m_edInfile->lineEdit()->text() + "\"";
	QString outputfile = "\"" + m_PostscriptDialog.m_edOutfile->lineEdit()->text() + "\"";
	bool viewer = m_PostscriptDialog.m_cbView->isChecked();
	
	bool equalfiles = false;
	if (inputfile == outputfile) {
		outputfile = tempname + ".tmp";
		equalfiles = true;
	}
	
	if (!zipped_psfile) {                                       // unzipped ps files
		if (m_PostscriptDialog.m_edOutfile->lineEdit()->text().isEmpty()) { // pstops/psselect | okular
			stream << command << " " << inputfile << " | okular -" << endl;
			viewer = false;
		} else {                                                    // pstops/psselect
			stream << command << " " << inputfile << " " << outputfile << endl;
		}
	} else {                                                      // zipped ps files
		if (m_PostscriptDialog.m_edOutfile->lineEdit()->text().isEmpty()) { // pstops/psselect | okular
			stream << "gunzip -c " << inputfile
			       << " | " << command
			       << " | okular -"
			       << endl;
			viewer = false;
		} else {
			stream << "gunzip -c " << inputfile                    // pstops/psselect
			       << " | " << command
			       << " > " << outputfile
			       << endl;
		}
	}

	// check, if we should stop
	if ( equalfiles || viewer ) {
		stream << "if [ $? != 0 ]; then" << endl;
		stream << "   exit 1" << endl;
		stream << "fi" << endl;
	}

	// replace the original file
	if ( equalfiles ) {
		stream << "rm " << inputfile << endl;
		stream << "mv " << outputfile << " " << inputfile << endl;
	}

	// viewer
	if ( viewer ) {                                                // viewer: okular
		stream << "okular" << " " 
		       << ((equalfiles) ? inputfile : outputfile) << endl;
	}

	// everything is prepared to do the job
	temp.close();

	return(tempname);
}

QString PostscriptDialog::duplicateParameter(const QString &param)
{
	QString s = QString::null;
	for (int i = 0; i < m_PostscriptDialog.m_spCopies->value(); ++i) {
		if (i == 0)
			s += param;
		else
			s += ',' + param;
	}

	return s;
}


bool PostscriptDialog::checkParameter()
{
	QString infile = m_PostscriptDialog.m_edInfile->lineEdit()->text();
	if (infile.isEmpty()) {
		showError(i18n("No input file is given."));
		return false;
	}

	QFileInfo fi(infile);
	QString suffix = fi.completeSuffix();
	if (suffix != "ps" && suffix != "ps.gz") {
		showError(i18n("Unknown file format: only '.ps' and '.ps.gz' are accepted for input files."));
		return false;
	}

	if (!fi.exists()) {
		showError(i18n("This input file does not exist."));
		return false;
	}

	// check parameter
	int index = m_PostscriptDialog.m_cbTask->currentItem();
	if (m_PostscriptDialog.m_edParameter->text().isEmpty()) {
		if (index == PS_PSSELECT_FREE) {
			showError( i18n("psselect needs some parameters in this mode.") );
			return false;
		} else if (index == PS_PSTOPS_FREE) {
			showError( i18n("pstops needs some parameters in this mode.") );
			return false;
		}
	}

	QString outfile = m_PostscriptDialog.m_edOutfile->lineEdit()->text();
	if (outfile.isEmpty() && !m_PostscriptDialog.m_cbView->isChecked()) {
		showError(i18n("You need to define an output file or select the viewer."));
		return false;
	}

	if (! outfile.isEmpty()) {
		QFileInfo fo(outfile);
		if (fo.completeSuffix() != "ps") {
			showError(i18n("Unknown file format: only '.ps' is accepted as output file."));
			return false;
		}
	
		if (infile != outfile && fo.exists()) {
			QString s = i18n("A file named \"%1\" already exists. Are you sure you want to overwrite it?", fo.fileName());
			if (KMessageBox::questionYesNo(this,
			                               "<center>" + s + "</center>",
			                               "Postscript tools") == KMessageBox::No) {
				return false;
			}
		}
	}
	
	return true;
}

void PostscriptDialog::comboboxChanged(int index)
{
	KILE_DEBUG() << index << endl;
	if (index == PS_COPY_SORTED || index == PS_COPY_UNSORTED) {
		m_PostscriptDialog.m_lbParameter->setEnabled(true);
		m_PostscriptDialog.m_lbParameter->setText(i18n("Copies:"));
		m_PostscriptDialog.m_edParameter->hide();
		m_PostscriptDialog.m_spCopies->show();
		m_PostscriptDialog.m_spCopies->setEnabled(true);
	} else {
		if (index == PS_PSSELECT_FREE || index == PS_PSTOPS_FREE) {
			m_PostscriptDialog.m_lbParameter->setEnabled(true);
			m_PostscriptDialog.m_lbParameter->setText(i18n("Parameter:"));
			m_PostscriptDialog.m_spCopies->hide();
			m_PostscriptDialog.m_edParameter->show();
			m_PostscriptDialog.m_edParameter->setEnabled(true);
		} else {
			m_PostscriptDialog.m_lbParameter->setEnabled(false);
			m_PostscriptDialog.m_edParameter->setEnabled(false);
			m_PostscriptDialog.m_spCopies->setEnabled(false);
		}
	}
}

void PostscriptDialog::showError(const QString &text)
{
	KMessageBox::error(this, i18n("<center>") + text + i18n("</center>"), i18n("Postscript Tools"));
}

}

#include "postscriptdialog.moc"
