/***************************************************************************
                          kile.cpp  -  description
                             -------------------
    begin                : sam jui 13 09:50:06 CEST 2002
    copyright            : (C) 2003 by Jeroen Wijnhout
    email                :  Jeroen.Wijnhout@kdemail.net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "kile.h"

#include <ktexteditor/editorchooser.h>
#include <ktexteditor/encodinginterface.h>
#include <ktexteditor/codecompletioninterface.h>
#include <ktexteditor/searchinterface.h>
#include <kparts/componentfactory.h>

#include <kdeversion.h>
#include <kdebug.h>
#include <kaboutdata.h>
#include <kfiledialog.h>
#include <klibloader.h>
#include <kiconloader.h>
#include <kstddirs.h>
#include <kmessagebox.h>
#include <kconfig.h>

#include <ksconfig.h>
#include <klocale.h>
#include <kglobalsettings.h>
#include <krun.h>
#include <khtmlview.h>
#include <kkeydialog.h>
#include <kedittoolbar.h>
#include <kglobal.h>
#include <kprinter.h>
#include <kwin.h>
#include <kparts/browserextension.h>
#include <kaccel.h>
#include <knuminput.h>
#include <klistview.h>
#include <kapplication.h>
#include <kstandarddirs.h>
#include <ktoolbarbutton.h>
#include <kmainwindow.h>

#include <qfileinfo.h>
#include <qregexp.h>
#include <qiconset.h>
#include <qtimer.h>
#include <qpopupmenu.h>
#include <ktabwidget.h>
#include <qapplication.h>
#include <qcombobox.h>
#include <qlineedit.h>
#include <qfile.h>
#include <qregexp.h>
#include <qtooltip.h>
#include <qvaluelist.h>
#include <qmap.h>
#include <qpainter.h>
#include <qpaintdevicemetrics.h>
#include <qtextstream.h>
#include <qtextcodec.h>
#include <qmetaobject.h>
#include <qvaluelist.h>
#include <qtextstream.h>
#include <qsignalmapper.h>

#include "kileapplication.h"
#include "kiledocumentinfo.h"
#include "kileactions.h"
#include "kilestdactions.h"
#include "usermenudialog.h"
#include "kileconfigdialog.h"
#include "kileproject.h"
#include "kileprojectview.h"
#include "kileprojectdlgs.h"
#include "kilelistselector.h"
#include "kilelyxserver.h"
#include "kilegrepdialog.h"
#include "kiletool_enums.h"
#include "kiletool.h"
#include "kiletoolmanager.h"
#include "kilestdtools.h"
#include "kilelogwidget.h"
#include "kileoutputwidget.h"
#include "kilekonsolewidget.h"
#include "quickdocumentdialog.h"
#include "tabdialog.h"
#include "arraydialog.h"
#include "tabbingdialog.h"
#include "kilestructurewidget.h"
#include "convert.h"
#include "includegraphicsdialog.h"
#include "cleandialog.h"
#include "kiledocmanager.h"
#include "kileviewmanager.h"
#include "kileeventfilter.h"
#include "kileautosavejob.h"
#include "kileconfig.h"
#include "kxtrcconverter.h"
#include "kileerrorhandler.h"
#include "configcheckerdlg.h"
#include "kilespell.h"
#include "kilespell2.h"

Kile::Kile( bool rest, QWidget *parent, const char *name ) :
	DCOPObject( "Kile" ),
	KParts::MainWindow( parent, name),
	KileInfo(this),
	m_paPrint(0L),
	m_bQuick(false),
	m_bShowUserMovedMessage(false)
{
	// do initializations first
	m_currentState = m_wantState="Editor";
	m_bWatchFile = m_logPresent = false;

	m_spell = new KileSpell(this, this, "kilespell");

	symbol_view = 0L;
	symbol_present=false;

	viewManager()->setClient(this, this);

	partManager = new KParts::PartManager( this );
	connect( partManager, SIGNAL( activePartChanged( KParts::Part * ) ), this, SLOT(ActivePartGUI ( KParts::Part * ) ) );

	m_AutosaveTimer= new QTimer();
	connect(m_AutosaveTimer,SIGNAL(timeout()),this,SLOT(autoSaveAll()));

	m_eventFilter = new KileEventFilter();
	connect(this,SIGNAL(configChanged()), m_eventFilter, SLOT(readConfig()));

	m_errorHandler = new KileErrorHandler(this, this);

	statusBar()->insertItem(i18n("Line: 1 Col: 1"), ID_LINE_COLUMN, 0, true);
	statusBar()->setItemAlignment( ID_LINE_COLUMN, AlignLeft|AlignVCenter );
	statusBar()->insertItem(i18n("Normal mode"), ID_HINTTEXT,10);
	statusBar()->setItemAlignment( ID_HINTTEXT, AlignLeft|AlignVCenter );
	topWidgetStack = new QWidgetStack( this );
	topWidgetStack->setFocusPolicy(QWidget::NoFocus);
	splitter1=new QSplitter(QSplitter::Horizontal,topWidgetStack, "splitter1" );

	Structview_layout=0;
	Structview=new QFrame(splitter1);
	Structview->setFrameStyle( QFrame::WinPanel | QFrame::Sunken );
	Structview->setLineWidth( 2 );
	Structview_layout=0;
	ButtonBar=new KMultiVertTabBar(Structview);

	ButtonBar->insertTab(SmallIcon("fileopen"),0,i18n("Open File"));
	connect(ButtonBar->getTab(0),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));
	KileFS= new KileFileSelect(Structview,"File Selector");
	connect(KileFS,SIGNAL(fileSelected(const KFileItem*)), docManager(), SLOT(fileSelected(const KFileItem*)));
	connect(KileFS->comboEncoding, SIGNAL(activated(int)),this,SLOT(changeInputEncoding()));
	KileFS->comboEncoding->lineEdit()->setText(input_encoding);

	KileProjectView *projectview = new KileProjectView(Structview, this);
	viewManager()->setProjectView(projectview);
	ButtonBar->insertTab( SmallIcon("editcopy"),9,i18n("Files & Projects"));
	connect(ButtonBar->getTab(9),SIGNAL(clicked(int)), this,SLOT(showVertPage(int)));
	connect(projectview, SIGNAL(fileSelected(const KileProjectItem *)), docManager(), SLOT(fileSelected(const KileProjectItem *)));
	connect(projectview, SIGNAL(fileSelected(const KURL &)), docManager(), SLOT(fileSelected(const KURL &)));
	connect(projectview, SIGNAL(closeURL(const KURL&)), docManager(), SLOT(fileClose(const KURL&)));
	connect(projectview, SIGNAL(closeProject(const KURL&)), docManager(), SLOT(projectClose(const KURL&)));
	connect(projectview, SIGNAL(projectOptions(const KURL&)), docManager(), SLOT(projectOptions(const KURL&)));
	connect(projectview, SIGNAL(projectArchive(const KURL&)), docManager(), SLOT(projectArchive(const KURL&)));
	connect(projectview, SIGNAL(removeFromProject(const KileProjectItem *)), docManager(), SLOT(removeFromProject(const KileProjectItem *)));
	connect(projectview, SIGNAL(addFiles(const KURL &)), docManager(), SLOT(projectAddFiles(const KURL &)));
	connect(projectview, SIGNAL(toggleArchive(KileProjectItem *)), docManager(), SLOT(toggleArchive(KileProjectItem *)));
	connect(projectview, SIGNAL(addToProject(const KURL &)), docManager(), SLOT(addToProject(const KURL &)));
	connect(projectview, SIGNAL(saveURL(const KURL &)), docManager(), SLOT(saveURL(const KURL &)));
	connect(projectview, SIGNAL(buildProjectTree(const KURL &)), docManager(), SLOT(buildProjectTree(const KURL &)));
	connect(docManager(), SIGNAL(projectTreeChanged(const KileProject *)), projectview, SLOT(refreshProjectTree(const KileProject *)));

	ButtonBar->insertTab( SmallIcon("structure"),1,i18n("Structure"));
	connect(ButtonBar->getTab(1),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));
	m_kwStructure = new KileWidget::Structure(this, Structview);
	m_kwStructure->setFocusPolicy(QWidget::ClickFocus);
	connect(m_kwStructure, SIGNAL(setCursor(const KURL &,int,int)), this, SLOT(setCursor(const KURL &,int,int)));
	connect(m_kwStructure, SIGNAL(fileOpen(const KURL&, const QString & )), docManager(), SLOT(fileOpen(const KURL&, const QString& )));
	connect(m_kwStructure, SIGNAL(fileNew(const KURL&)), docManager(), SLOT(fileNew(const KURL&)));

	QToolTip::add(m_kwStructure, i18n("Click to jump to the line"));

	mpview = new metapostview( Structview );
	connect(mpview, SIGNAL(clicked(QListBoxItem *)), SLOT(InsertMetaPost(QListBoxItem *)));

	m_edit = new KileDocument::EditorExtension(this);
	m_help = new KileHelp::Help(m_edit);

	config = KGlobal::config();

	// check requirements for IncludeGraphicsDialog (dani)
	KileConfig::setImagemagick(!(KStandardDirs::findExe("identify") == QString::null));

	//workaround for kdvi crash when started with Tooltips
	KileConfig::setRunOnStart(false);

	KileFS->readConfig();

	setXMLFile( "kileui.rc" );

	ReadSettings();

	setupActions();

	// ReadRecentFileSettings should be after setupActions() because fileOpenRecentAction needs to be
	// initialized before calling ReadSettnigs().
	ReadRecentFileSettings();

	ButtonBar->insertTab(SmallIcon("math1"),2,i18n("Relation Symbols"));
	connect(ButtonBar->getTab(2),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));
	ButtonBar->insertTab(SmallIcon("math2"),3,i18n("Arrow Symbols"));
	connect(ButtonBar->getTab(3),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));
	ButtonBar->insertTab(SmallIcon("math3"),4,i18n("Miscellaneous Symbols"));
	connect(ButtonBar->getTab(4),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));
	ButtonBar->insertTab(SmallIcon("math4"),5,i18n("Delimiters"));
	connect(ButtonBar->getTab(5),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));
	ButtonBar->insertTab(SmallIcon("math5"),6,i18n("Greek Letters"));
	connect(ButtonBar->getTab(6),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));
	ButtonBar->insertTab(SmallIcon("math6"),7,i18n("Special Characters"));
	connect(ButtonBar->getTab(7),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));
	ButtonBar->insertTab(SmallIcon("metapost"),8,i18n("MetaPost Commands"));
	connect(ButtonBar->getTab(8),SIGNAL(clicked(int)),this,SLOT(showVertPage(int)));

	splitter2=new QSplitter(QSplitter::Vertical, splitter1, "splitter2");

	viewManager()->createTabs(splitter2);
	connect(viewManager(), SIGNAL(activateView(QWidget*, bool)), this, SLOT(activateView(QWidget*, bool)));
	connect(viewManager(), SIGNAL(prepareForPart(const QString& )), this, SLOT(prepareForPart(const QString& )));

	//Log/Messages/KShell widgets
	m_outputView = new KTabWidget(splitter2);
	m_outputView->setFocusPolicy(QWidget::ClickFocus);

	//m_logWidget = new MessageWidget( m_outputView );
	m_logWidget = new KileWidget::LogMsg( this, m_outputView );
	connect(m_logWidget, SIGNAL(fileOpen(const KURL&, const QString & )), docManager(), SLOT(fileOpen(const KURL&, const QString& )));
	connect(m_logWidget, SIGNAL(setLine(const QString& )), this, SLOT(setLine(const QString& )));

	m_logWidget->setFocusPolicy(QWidget::ClickFocus);
	m_logWidget->setMinimumHeight(40);
	m_logWidget->setReadOnly(true);
	m_outputView->addTab(m_logWidget,SmallIcon("viewlog"), i18n("Log/Messages"));

	m_outputWidget = new KileWidget::Output(m_outputView);
	m_outputWidget->setFocusPolicy(QWidget::ClickFocus);
	m_outputWidget->setMinimumHeight(40);
	m_outputWidget->setReadOnly(true);
	m_outputView->addTab(m_outputWidget,SmallIcon("output_win"), i18n("Output"));

	m_outputInfo=new LatexOutputInfoArray();
	m_outputFilter=new LatexOutputFilter(m_outputInfo);
	connect(m_outputFilter, SIGNAL(problem(int, const QString& )), m_logWidget, SLOT(printProblem(int, const QString& )));

	m_texKonsole=new KileWidget::Konsole(this, m_outputView,"konsole");
	m_outputView->addTab(m_texKonsole,SmallIcon("konsole"),i18n("Konsole"));

	QValueList<int> sizes;
	sizes << split2_top << split2_bottom;
	splitter2->setSizes( sizes );
	sizes.clear();
	sizes << split1_left << split1_right;
	splitter1->setSizes( sizes );

	topWidgetStack->addWidget(splitter1 , 0);
	setCentralWidget(topWidgetStack);
	ShowOutputView(false);
	ShowStructView(false);
	m_outputView->showPage(m_logWidget);
	newCaption();
	showVertPage(lastvtab);
	m_singlemode=true;
	m_masterName=getName();

	partManager->setActivePart( 0L );

	m_lyxserver = new KileLyxServer(m_runlyxserver);
	connect(m_lyxserver, SIGNAL(insert(const KileAction::TagData &)), this, SLOT(insertTag(const KileAction::TagData &)));

	KileApplication::closeSplash();
	show();

	connect(m_outputView, SIGNAL( currentChanged( QWidget * ) ), m_texKonsole, SLOT(sync()));

	applyMainWindowSettings(config, "KileMainWindow" );

	m_manager  = new KileTool::Manager(this, config, m_logWidget, m_outputWidget, partManager, topWidgetStack, m_paStop, 10000); //FIXME make timeout configurable
	connect(m_manager, SIGNAL(requestGUIState(const QString &)), this, SLOT(prepareForPart(const QString &)));
	connect(m_manager, SIGNAL(requestSaveAll()), docManager(), SLOT(fileSaveAll()));
	connect(m_manager, SIGNAL(jumpToFirstError()), m_errorHandler, SLOT(jumpToFirstError()));
	connect(m_manager, SIGNAL(toolStarted()), m_errorHandler, SLOT(reset()));

	m_toolFactory = new KileTool::Factory(m_manager, config);
	m_manager->setFactory(m_toolFactory);
	m_help->setManager(m_manager);     // kile help (dani)

	if ( m_listUserTools.count() > 0 )
	{
		KMessageBox::information(0, i18n("You have defined some tools in the User menu. From now on these tools will be available from the Build->Other menu and can be configured in the configuration dialog (go to the Settings menu and choose Configure Kile). This has some advantages; your own tools can now be used in a QuickBuild command if you wish."), i18n("User Tools Detected"));
		m_listUserTools.clear();
	}

	if (m_bShowUserMovedMessage)
	{
		KMessageBox::information(0, i18n("Please note that the 'User' menu, which holds the (La)TeX tags you have defined, is moved to the LaTeX menu."));
	}

	connect(docManager(), SIGNAL(updateModeStatus()), this, SLOT(updateModeStatus()));
	connect(docManager(), SIGNAL(updateStructure(bool, KileDocument::Info*)), viewManager(), SLOT(updateStructure(bool, KileDocument::Info*)));
	connect(docManager(), SIGNAL(closingDocument(KileDocument::Info* )), m_kwStructure, SLOT(closeDocumentInfo(KileDocument::Info *)));
	connect(docManager(), SIGNAL(documentInfoCreated(KileDocument::Info* )), m_kwStructure, SLOT(addDocumentInfo(KileDocument::Info* )));

// 	connect(viewManager(), SIGNAL(updateStructure(bool, KileDocument::Info*)), this, SLOT(UpdateStructure(bool, KileDocument::Info*)));

	if (rest) restore();
}

Kile::~Kile()
{
	kdDebug() << "cleaning up..." << endl;

	// CodeCompletion  and edvanced editor (dani)
	delete m_edit;
	delete m_AutosaveTimer;
}

void Kile::setupActions()
{
	m_paPrint = KStdAction::print(0,0, actionCollection(), "print");
	(void) KStdAction::openNew(docManager(), SLOT(fileNew()), actionCollection(), "file_new" );
	(void) KStdAction::open(docManager(), SLOT(fileOpen()), actionCollection(),"file_open" );
	fileOpenRecentAction = KStdAction::openRecent(docManager(), SLOT(fileOpen(const KURL&)), actionCollection(), "file_open_recent");
	connect(docManager(), SIGNAL(addToRecentFiles(const KURL& )), fileOpenRecentAction, SLOT(addURL(const KURL& )));

	(void) new KAction(i18n("Save All"),"save_all", 0, docManager(), SLOT(fileSaveAll()), actionCollection(),"file_save_all" );
	(void) new KAction(i18n("Create Template From Document..."), 0, docManager(), SLOT(createTemplate()), actionCollection(),"CreateTemplate");
	(void) KStdAction::close(docManager(), SLOT(fileClose()), actionCollection(),"file_close" );
	(void) new KAction(i18n("Close All"), 0, docManager(), SLOT(fileCloseAll()), actionCollection(),"file_close_all" );
	(void) new KAction(i18n("S&tatistics"), 0, this, SLOT(showDocInfo()), actionCollection(), "Statistics" );
	(void) new KAction(i18n("&ASCII"), 0, this, SLOT(convertToASCII()), actionCollection(), "file_export_ascii" );
	(void) new KAction(i18n("Latin-&1 (iso 8859-1)"), 0, this, SLOT(convertToEnc()), actionCollection(), "file_export_latin1" );
	(void) new KAction(i18n("Latin-&2 (iso 8859-2)"), 0, this, SLOT(convertToEnc()), actionCollection(), "file_export_latin2" );
	(void) new KAction(i18n("Latin-&3 (iso 8859-3)"), 0, this, SLOT(convertToEnc()), actionCollection(), "file_export_latin3" );
	(void) new KAction(i18n("Latin-&4 (iso 8859-4)"), 0, this, SLOT(convertToEnc()), actionCollection(), "file_export_latin4" );
	(void) new KAction(i18n("Latin-&5 (iso 8859-5)"), 0, this, SLOT(convertToEnc()), actionCollection(), "file_export_latin5" );
	(void) new KAction(i18n("Latin-&9 (iso 8859-9)"), 0, this, SLOT(convertToEnc()), actionCollection(), "file_export_latin9" );
	(void) new KAction(i18n("&Central European (cp-1250)"), 0, this, SLOT(convertToEnc()), actionCollection(), "file_export_cp1250" );
	(void) new KAction(i18n("&Western European (cp-1252)"), 0, this, SLOT(convertToEnc()), actionCollection(), "file_export_cp1252" );
	(void) KStdAction::quit(this, SLOT(close()), actionCollection(),"file_quit" );

	(void) new KAction(i18n("Find &in Files..."), ALT+SHIFT+Key_F, this, SLOT(FindInFiles()), actionCollection(),"FindInFiles" );

	kdDebug() << "CONNECTING SPELLCHECKER" << endl;
	connect ( viewManager(), SIGNAL(startSpellCheck()), m_spell, SLOT(spellcheck()) );

	(void) new KAction(i18n("Refresh Structure"), "structure", 0, this, SLOT(RefreshStructure()), actionCollection(),"RefreshStructure" );

	//project actions
	(void) new KAction(i18n("&New Project..."), "filenew", 0, docManager(), SLOT(projectNew()), actionCollection(), "project_new");
	(void) new KAction(i18n("&Open Project..."), "fileopen", 0, docManager(), SLOT(projectOpen()), actionCollection(), "project_open");
	m_actRecentProjects =  new KRecentFilesAction(i18n("Open &Recent Project"),  0, docManager(), SLOT(projectOpen(const KURL &)), actionCollection(), "project_openrecent");
	connect(docManager(), SIGNAL(removeFromRecentProjects(const KURL& )), m_actRecentProjects, SLOT(removeURL(const KURL& )));
	connect(docManager(), SIGNAL(addToRecentProjects(const KURL& )), m_actRecentProjects, SLOT(addURL(const KURL& )));

	(void) new KAction(i18n("A&dd Files to Project..."), 0, docManager(), SLOT(projectAddFiles()), actionCollection(), "project_add");
	(void) new KAction(i18n("Refresh Project &Tree"), "relation", 0, docManager(), SLOT(buildProjectTree()), actionCollection(), "project_buildtree");
	(void) new KAction(i18n("&Archive"), "package", 0, docManager(), SLOT(projectArchive()), actionCollection(), "project_archive");
	(void) new KAction(i18n("Project &Options"), "configure", 0, docManager(), SLOT(projectOptions()), actionCollection(), "project_options");
	(void) new KAction(i18n("&Close Project"), "fileclose", 0, docManager(), SLOT(projectClose()), actionCollection(), "project_close");

	//build actions
	(void) new KAction(i18n("Clean"),0 , this, SLOT(CleanAll()), actionCollection(),"CleanAll" );
	(void) new KAction(i18n("View Log File"),"viewlog", ALT+Key_0, m_errorHandler, SLOT(ViewLog()), actionCollection(),"ViewLog" );
	(void) new KAction(i18n("Previous LaTeX Error"),"errorprev", 0, m_errorHandler, SLOT(PreviousError()), actionCollection(),"PreviousError" );
	(void) new KAction(i18n("Next LaTeX Error"),"errornext", 0, m_errorHandler, SLOT(NextError()), actionCollection(),"NextError" );
	(void) new KAction(i18n("Previous LaTeX Warning"),"warnprev", 0, m_errorHandler, SLOT(PreviousWarning()), actionCollection(),"PreviousWarning" );
	(void) new KAction(i18n("Next LaTeX Warning"),"warnnext", 0, m_errorHandler, SLOT(NextWarning()), actionCollection(),"NextWarning" );
	(void) new KAction(i18n("Previous LaTeX BadBox"),"bboxprev", 0, m_errorHandler, SLOT(PreviousBadBox()), actionCollection(),"PreviousBadBox" );
	(void) new KAction(i18n("Next LaTeX BadBox"),"bboxnext", 0, m_errorHandler, SLOT(NextBadBox()), actionCollection(),"NextBadBox" );
	m_paStop = new KAction(i18n("&Stop"),"stop",Key_Escape,0,0,actionCollection(),"Stop");
	m_paStop->setEnabled(false);

	setupTools();

	(void) new KAction(i18n("Editor View"),"edit",CTRL+Key_E , this, SLOT(ShowEditorWidget()), actionCollection(),"EditorView" );
	(void) new KAction(i18n("Next Document"),"forward",ALT+Key_Right, viewManager(), SLOT(gotoNextView()), actionCollection(), "gotoNextDocument" );
	(void) new KAction(i18n("Previous Document"),"back",ALT+Key_Left, viewManager(), SLOT(gotoPrevView()), actionCollection(), "gotoPrevDocument" );
	(void) new KAction(i18n("Focus Log/Messages View"), CTRL+ALT+Key_M, this, SLOT(focusLog()), actionCollection(), "focus_log");
	(void) new KAction(i18n("Focus Output View"), CTRL+ALT+Key_O, this, SLOT(focusOutput()), actionCollection(), "focus_output");
	(void) new KAction(i18n("Focus Konsole View"), CTRL+ALT+Key_K, this, SLOT(focusKonsole()), actionCollection(), "focus_konsole");
	(void) new KAction(i18n("Focus Editor View"), CTRL+ALT+Key_E, this, SLOT(focusEditor()), actionCollection(), "focus_editor");

 // CodeCompletion (dani)
	(void) new KAction(i18n("La(TeX) Command"),"complete1",CTRL+Key_Space, m_edit, SLOT(completeWord()), actionCollection(), "edit_complete_word");
	(void) new KAction(i18n("Environment"),"complete2",ALT+Key_Space, m_edit, SLOT(completeEnvironment()), actionCollection(), "edit_complete_env");
	(void) new KAction(i18n("Abbreviation"),"complete3",CTRL+ALT+Key_Space, m_edit, SLOT(completeAbbreviation()), actionCollection(), "edit_complete_abbrev");
	(void) new KAction(i18n("Next Bullet"),"nextbullet",CTRL+ALT+Key_Right, m_edit, SLOT(nextBullet()), actionCollection(), "edit_next_bullet");
	(void) new KAction(i18n("Prev Bullet"),"prevbullet",CTRL+ALT+Key_Left, m_edit, SLOT(prevBullet()), actionCollection(), "edit_prev_bullet");

 // advanced editor (dani)
	(void) new KAction(i18n("Environment (inside)"),KShortcut("CTRL+Alt+S,E"), m_edit, SLOT(selectEnvInside()), actionCollection(), "edit_select_inside_env");
	(void) new KAction(i18n("Environment (outside)"),KShortcut("CTRL+Alt+S,F"), m_edit, SLOT(selectEnvOutside()), actionCollection(), "edit_select_outside_env");
	(void) new KAction(i18n("TeX Group (inside)"),"selgroup_i",KShortcut("CTRL+Alt+S,T"), m_edit, SLOT(selectTexgroupInside()), actionCollection(), "edit_select_inside_group");
	(void) new KAction(i18n("TeX Group (outside)"),"selgroup_o",KShortcut("CTRL+Alt+S,U"),m_edit, SLOT(selectTexgroupOutside()), actionCollection(), "edit_select_outside_group");
	(void) new KAction(i18n("Paragraph"),KShortcut("CTRL+Alt+S,P"),m_edit, SLOT(selectParagraph()), actionCollection(), "edit_select_paragraph");
	(void) new KAction(i18n("Line"),KShortcut("CTRL+Alt+S,L"),m_edit, SLOT(selectLine()), actionCollection(), "edit_select_line");
	(void) new KAction(i18n("TeX Word"),KShortcut("CTRL+Alt+S,W"),m_edit, SLOT(selectWord()), actionCollection(), "edit_select_word");

	(void) new KAction(i18n("Environment (inside)"),KShortcut("CTRL+Alt+D,E"), m_edit, SLOT(deleteEnvInside()), actionCollection(), "edit_delete_inside_env");
	(void) new KAction(i18n("Environment (outside)"),KShortcut("CTRL+Alt+D,F"),m_edit, SLOT(deleteEnvOutside()), actionCollection(), "edit_delete_outside_env");
	(void) new KAction(i18n("TeX Group (inside)"),KShortcut("CTRL+Alt+D,T"), m_edit, SLOT(deleteTexgroupInside()), actionCollection(), "edit_delete_inside_group");
	(void) new KAction(i18n("TeX Group (outside)"),KShortcut("CTRL+Alt+D,U"),m_edit, SLOT(deleteTexgroupInside()), actionCollection(), "edit_delete_outside_group");
	(void) new KAction(i18n("Paragraph"),KShortcut("CTRL+Alt+D,P"),m_edit, SLOT(deleteParagraph()), actionCollection(), "edit_delete_paragraph");
	(void) new KAction(i18n("TeX Word"),KShortcut("CTRL+Alt+D,W"),m_edit, SLOT(deleteWord()), actionCollection(), "edit_delete_word");

	(void) new KAction(i18n("Goto Begin"),KShortcut("CTRL+Alt+E,B"), m_edit, SLOT(gotoBeginEnv()), actionCollection(), "edit_begin_env");
	(void) new KAction(i18n("Goto End"),KShortcut("CTRL+Alt+E,E"), m_edit, SLOT(gotoEndEnv()), actionCollection(), "edit_end_env");
	(void) new KAction(i18n("Match"),"matchenv",KShortcut("CTRL+Alt+E,M"), m_edit, SLOT(matchEnv()), actionCollection(), "edit_match_env");
	(void) new KAction(i18n("Close"),"closeenv",KShortcut("CTRL+Alt+E,C"), m_edit, SLOT(closeEnv()), actionCollection(), "edit_close_env");

	(void) new KAction(i18n("Goto Begin"),KShortcut("CTRL+Alt+G,B"), m_edit, SLOT(gotoBeginTexgroup()), actionCollection(), "edit_begin_group");
	(void) new KAction(i18n("Goto End"),KShortcut("CTRL+Alt+G,E"), m_edit, SLOT(gotoEndTexgroup()), actionCollection(), "edit_end_group");
	(void) new KAction(i18n("Match"),"matchgroup",KShortcut("CTRL+Alt+G,M"), m_edit, SLOT(matchTexgroup()), actionCollection(), "edit_match_group");
	(void) new KAction(i18n("Close"),"closegroup",KShortcut("CTRL+Alt+G,C"), m_edit, SLOT(closeTexgroup()), actionCollection(), "edit_close_group");

	(void) new KAction(i18n("teTeX Guide"),KShortcut("CTRL+Alt+H,T"), m_help, SLOT(helpTetexGuide()), actionCollection(), "edit_help_tetex_guide");
	(void) new KAction(i18n("teTeX Doc"),KShortcut("CTRL+Alt+H,T"), m_help, SLOT(helpTetexDoc()), actionCollection(), "edit_help_tetex_doc");
	(void) new KAction(i18n("LaTeX"),KShortcut("CTRL+Alt+H,L"), m_help, SLOT(helpLatexIndex()), actionCollection(), "edit_help_latex_index");
	(void) new KAction(i18n("LaTeX Command"),KShortcut("CTRL+Alt+H,C"), m_help, SLOT(helpLatexCommand()), actionCollection(), "edit_help_latex_command");
	(void) new KAction(i18n("LaTeX Subject"),KShortcut("CTRL+Alt+H,S"), m_help, SLOT(helpLatexSubject()), actionCollection(), "edit_help_latex_subject");
	(void) new KAction(i18n("LaTeX Env"),KShortcut("CTRL+Alt+H,E"), m_help, SLOT(helpLatexEnvironment()), actionCollection(), "edit_help_latex_env");
	(void) new KAction(i18n("Context Help"),KShortcut("CTRL+Alt+H,K"), m_help, SLOT(helpKeyword()), actionCollection(), "edit_help_context");

	KileStdActions::setupStdTags(this,this);
	KileStdActions::setupMathTags(this);
	KileStdActions::setupBibTags(this);

	(void) new KAction(i18n("Quick Start"),"wizard",0 , this, SLOT(QuickDocument()), actionCollection(),"127" );
	connect(docManager(), SIGNAL(startWizard()), this, SLOT(QuickDocument()));
	(void) new KAction(i18n("Tabular"),"wizard",0 , this, SLOT(QuickTabular()), actionCollection(),"129" );
	(void) new KAction(i18n("Tabbing"),"wizard",0 , this, SLOT(QuickTabbing()), actionCollection(),"149" );
	(void) new KAction(i18n("Array"),"wizard",0 , this, SLOT(QuickArray()), actionCollection(),"130" );


	(void) new KAction(i18n("Clean"),0 , this, SLOT(CleanBib()), actionCollection(),"CleanBib" );

	ModeAction=new KToggleAction(i18n("Define Current Document as '&Master Document'"),"master",0 , this, SLOT(ToggleMode()), actionCollection(),"Mode" );

	StructureAction=new KToggleAction(i18n("Show Str&ucture View"),0 , this, SLOT(ToggleStructView()), actionCollection(),"StructureView" );
	MessageAction=new KToggleAction(i18n("Show Mess&ages View"),0 , this, SLOT(ToggleOutputView()), actionCollection(),"MessageView" );

	//FIXME: obsolete for KDE 4
	m_paShowMainTB = new KToggleToolBarAction("mainToolBar", i18n("Main"), actionCollection(), "ShowMainToolbar");
	m_paShowToolsTB = new KToggleToolBarAction("toolsToolBar", i18n("Tools"), actionCollection(), "ShowToolsToolbar");
	m_paShowBuildTB = new KToggleToolBarAction("buildToolBar", i18n("Build"), actionCollection(), "ShowQuickToolbar");
	m_paShowErrorTB = new KToggleToolBarAction("errorToolBar", i18n("Error"), actionCollection(), "ShowErrorToolbar");
	m_paShowEditTB = new KToggleToolBarAction("editToolBar", i18n("Edit"), actionCollection(), "ShowEditToolbar");
	m_paShowMathTB = new KToggleToolBarAction("mathToolBar", i18n("Math"), actionCollection(), "ShowMathToolbar");

	//Save the toolbar settings, we need to know if the toolbars should be visible or not. We can't use KToggleToolBarAction->isChecked()
	//since this will return false if we hide the toolbar when switching to Viewer mode for example.
	m_bShowMainTB = m_paShowMainTB->isChecked();
	m_bShowToolsTB = m_paShowToolsTB->isChecked();
	m_bShowErrorTB = m_paShowErrorTB->isChecked();
	m_bShowBuildTB = m_paShowBuildTB->isChecked();
	m_bShowEditTB = m_paShowEditTB->isChecked();
	m_bShowMathTB = m_paShowMathTB->isChecked();

	if (m_singlemode) {ModeAction->setChecked(false);}
	else {ModeAction->setChecked(true);}
	if (showstructview) {StructureAction->setChecked(true);}
	else {StructureAction->setChecked(false);}
	if (showoutputview) {MessageAction->setChecked(true);}
	else {MessageAction->setChecked(false);}

	(void) new KAction(i18n("&Remove Template..."),0, docManager(), SLOT(removeTemplate()), actionCollection(), "removetemplates");

	WatchFileAction=new KToggleAction(i18n("Watch File Mode"),"watchfile",0 , this, SLOT(ToggleWatchFile()), actionCollection(), "WatchFile");
	if (m_bWatchFile) {WatchFileAction->setChecked(true);}
	else {WatchFileAction->setChecked(false);}

	setHelpMenuEnabled(false);
	const KAboutData *aboutData = KGlobal::instance()->aboutData();
	KHelpMenu *help_menu = new KHelpMenu( this, aboutData);
	(void) new KAction(i18n("LaTeX Reference"),"help",0 , this, SLOT(LatexHelp()), actionCollection(),"help1" );
	(void) KStdAction::helpContents(help_menu, SLOT(appHelpActivated()), actionCollection(), "help2");
	(void) KStdAction::reportBug (help_menu, SLOT(reportBug()), actionCollection(), "report_bug");
	(void) KStdAction::aboutApp(help_menu, SLOT(aboutApplication()), actionCollection(),"help4" );
	(void) KStdAction::aboutKDE(help_menu, SLOT(aboutKDE()), actionCollection(),"help5" );
	(void) KStdAction::preferences(this, SLOT(GeneralOptions()), actionCollection(),"settings_configure" );
	(void) KStdAction::keyBindings(this, SLOT(ConfigureKeys()), actionCollection(),"147" );
	(void) KStdAction::configureToolbars(this, SLOT(ConfigureToolbars()), actionCollection(),"148" );
	new KAction(i18n("&System Check..."), 0, this, SLOT(slotPerformCheck()), actionCollection(), "settings_perform_check");

	m_menuUserTags = new KActionMenu(i18n("User Tags"), SmallIcon("label"), actionCollection(),"menuUserTags");
	m_menuUserTags->setDelayed(false);
	setupUserTagActions();

	actionCollection()->readShortcutSettings();
	
	m_bFullScreen = false;
  	m_pFullScreen = KStdAction::fullScreen(this, SLOT(slotToggleFullScreen()), actionCollection(), this);
}

void Kile::setupTools()
{
	kdDebug() << "==Kile::setupTools()===================" << endl;
	QStringList tools = KileTool::toolList(config);
	QString toolMenu;
	QPtrList<KAction> *pl;

	unplugActionList("list_compilers");
	unplugActionList("list_converters");
	unplugActionList("list_quickies");
	unplugActionList("list_viewers");
	unplugActionList("list_other");

	for ( uint i = 0; i < tools.count(); i++)
	{
		QString grp = KileTool::groupFor(tools[i], config);
		kdDebug() << tools[i] << " is using group: " << grp << endl;
		config->setGroup(KileTool::groupFor(tools[i], config));
		toolMenu = KileTool::menuFor(tools[i], config);

		if ( toolMenu == "none" ) continue;

		if ( toolMenu == "Compile" )
			pl = &m_listCompilerActions;
		else if ( toolMenu == "View" )
			pl = &m_listViewerActions;
		else if ( toolMenu == "Convert" )
			pl = &m_listConverterActions;
		else if ( toolMenu == "Quick" )
			pl = &m_listQuickActions;
		else
			pl = &m_listOtherActions;

		kdDebug() << "\tadding " << tools[i] << " " << toolMenu << " #" << pl->count() << endl;

		if ( action(QString("tool_"+tools[i]).ascii()) == 0L )
		{
			KAction *act = new KAction(tools[i], KileTool::iconFor(tools[i], config), KShortcut(), this, SLOT(runTool()), actionCollection(), QString("tool_"+tools[i]).ascii());
			pl->append(act);
		}
	}

	cleanUpActionList(m_listCompilerActions, tools);
	cleanUpActionList(m_listViewerActions, tools);
	cleanUpActionList(m_listConverterActions, tools);
	cleanUpActionList(m_listQuickActions, tools);
	cleanUpActionList(m_listOtherActions, tools);

	plugActionList("list_compilers", m_listCompilerActions);
	plugActionList("list_viewers", m_listViewerActions);
	plugActionList("list_converters", m_listConverterActions);
	plugActionList("list_quickies", m_listQuickActions);
	plugActionList("list_other", m_listOtherActions);

	actionCollection()->readShortcutSettings("Shortcuts", config);
}

void Kile::cleanUpActionList(QPtrList<KAction> &list, const QStringList & tools)
{
	for ( KAction *act = list.first(); act; act = list.next() )
	{
		if ( action(act->name()) != 0L && !tools.contains(QString(act->name()).mid(5)) )
		{
			list.remove(act);
			if ( act->isPlugged(toolBar("toolsToolBar")) ) act->unplug(toolBar("toolsToolBar"));
		}
	}
}

void Kile::setupUserTagActions()
{
	KShortcut tagaccels[10] = {CTRL+SHIFT+Key_1, CTRL+SHIFT+Key_2,CTRL+SHIFT+Key_3,CTRL+SHIFT+Key_4,CTRL+SHIFT+Key_5,CTRL+SHIFT+Key_6,CTRL+SHIFT+Key_7,
		CTRL+SHIFT+Key_8,CTRL+SHIFT+Key_9,CTRL+SHIFT+Key_0};

	m_actionEditTag = new KAction(i18n("Edit User Tags"),0 , this, SLOT(EditUserMenu()), m_menuUserTags,"EditUserMenu" );
	m_menuUserTags->insert(m_actionEditTag);
	for (uint i=0; i<m_listUserTags.size(); i++)
	{
		KShortcut sc; if (i<10)  { sc = tagaccels[i]; } else { sc = 0; }
		QString name = QString::number(i+1)+": "+m_listUserTags[i].text;
		KileAction::Tag *menuItem = new KileAction::Tag(name, sc, this, SLOT(insertTag(const KileAction::TagData &)), actionCollection(), QString("tag_user_" + m_listUserTags[i].text).ascii(), m_listUserTags[i]);
		m_listUserTagsActions.append(menuItem);
		m_menuUserTags->insert(menuItem);
	}

	actionCollection()->readShortcutSettings("Shortcuts", config);
}

void Kile::restore()
{
	if (!m_bRestore) return;

	QFileInfo fi;

	for (uint i=0; i < m_listProjectsOpenOnStart.count(); i++)
	{
		kdDebug() << "restoring " << m_listProjectsOpenOnStart[i] << endl;
		fi.setFile(m_listProjectsOpenOnStart[i]);
		if (fi.isReadable())
			docManager()->projectOpen(KURL::fromPathOrURL(m_listProjectsOpenOnStart[i]),
				i, m_listProjectsOpenOnStart.count());
	}

	for (uint i=0; i < m_listDocsOpenOnStart.count(); i++)
	{
		kdDebug() << "restoring " << m_listDocsOpenOnStart[i] << endl;
		fi.setFile(m_listDocsOpenOnStart[i]);
		if (fi.isReadable())
			docManager()->fileOpen(KURL::fromPathOrURL(m_listDocsOpenOnStart[i]));
	}

	m_masterName = KileConfig::master();
	m_singlemode = (m_masterName == "");
	if (ModeAction) ModeAction->setChecked(!m_singlemode);
	updateModeStatus();

	m_listProjectsOpenOnStart.clear();
	m_listDocsOpenOnStart.clear();

	Kate::Document *doc = docManager()->docFor(KURL::fromPathOrURL(lastDocument));
	if (doc) activateView(doc->views().first());
}

void Kile::setActive()
{
	kdDebug() << "ACTIVATING" << endl;
	kapp->mainWidget()->raise();
	kapp->mainWidget()->setActiveWindow();
}

////////////////////////////// FILE /////////////////////////////

void Kile::setLine( const QString &line )
{
	bool ok;
	uint l=line.toUInt(&ok,10);
	Kate::View *view = viewManager()->currentView();
	if (view && ok)
  	{
		this->show();
		this->raise();
		view->setFocus();
		view->gotoLineNumber(l);

		ShowEditorWidget();
		newStatus();
  	}
}

void Kile::setCursor(const KURL &url, int parag, int index)
{
	Kate::Document *doc = docManager()->docFor(url);
	if (doc) 
	{
		Kate::View *view = (Kate::View*)doc->views().first();
		if (view)
		{
			view->setCursorPositionReal(parag, index);
			view->setFocus();
		}
	}
}

void Kile::load(const QString &path)
{
	docManager()->load(KURL::fromPathOrURL(path));
}

int Kile::run(const QString & tool)
{
	return m_manager->runBlocking(tool);
}

int Kile::runWith(const QString &tool, const QString &config)
{
	return m_manager->runBlocking(tool, config);
}

//TODO: move to KileView::Manager
void Kile::activateView(QWidget* w, bool updateStruct /* = true */ )  //Needs to be QWidget because of QTabWidget::currentChanged
{
	//kdDebug() << "==Kile::activateView==========================" << endl;
	if (!w->inherits("Kate::View"))
		return;

	Kate::View* view = (Kate::View*)w;

	for (uint i=0; i< viewManager()->views().count(); i++)
	{
		guiFactory()->removeClient(viewManager()->view(i));
		viewManager()->view(i)->setActive(false);
	}

	toolBar ()->setUpdatesEnabled (false);

	guiFactory()->addClient(view);
	view->setActive( true );

	if (updateStruct) viewManager()->updateStructure();

	toolBar ()->setUpdatesEnabled (true);
}

void Kile::updateModeStatus()
{
	KileProject *project = docManager()->activeProject();

	if (project)
	{
		statusBar()->changeItem(i18n("Project: %1").arg(project->name()), ID_HINTTEXT);
	}
	else
	{
		if (m_singlemode)
		{
			statusBar()->changeItem(i18n("Normal mode"), ID_HINTTEXT);
		}
		else
		{
			QString shortName = m_masterName;
			int pos = shortName.findRev('/');
			shortName.remove(0,pos+1);
			statusBar()->changeItem(i18n("Master document: %1").arg(shortName), ID_HINTTEXT);
		}
	}
}

void Kile::open(const QString & url)
{
	docManager()->fileSelected(KURL::fromPathOrURL(url));
}

void Kile::close()
{
	docManager()->fileClose();
}

void Kile::autoSaveAll()
{
	docManager()->fileSaveAll(true);
}

void Kile::enableAutosave(bool as)
{
	autosave=as;
	if (as)
	{
		//paranoia pays, we're really screwed if somehow autosaveinterval equals zero
		if ( autosaveinterval < 1 ) autosaveinterval = 10;
		m_AutosaveTimer->start(autosaveinterval * 60000);
	}
	else m_AutosaveTimer->stop();
}

void Kile::projectOpen(const QString& proj)
{
	docManager()->projectOpen(KURL::fromPathOrURL(proj));
}

void Kile::focusLog()
{
	m_outputView->showPage(m_logWidget);
}

void Kile::focusOutput()
{
	m_outputView->showPage(m_outputWidget);
}

void Kile::focusKonsole()
{
	m_outputView->showPage(m_texKonsole);
}

void Kile::focusEditor()
{
	Kate::View *view = viewManager()->currentView();
	if (view) view->setFocus();
}

bool Kile::queryExit()
{
	SaveSettings();
	return true;
}

bool Kile::queryClose()
{
	//don't close Kile if embedded viewers are present
	if ( m_currentState != "Editor" )
	{
		ResetPart();
		return false;
	}

	Kate::View *view = viewManager()->currentView();
	if (view)
		lastDocument = view->getDoc()->url().path();

	m_listProjectsOpenOnStart.clear();
	m_listDocsOpenOnStart.clear();

	kdDebug() << "#projects = " << docManager()->projects()->count() << endl;
	for (uint i=0; i < docManager()->projects()->count(); i++)
	{
		m_listProjectsOpenOnStart.append(docManager()->projects()->at(i)->url().path());
	}

	bool stage1 = docManager()->projectCloseAll();
	bool stage2 = true;

	if (stage1)
	{
		for (uint i=0; i < viewManager()->views().count(); i++)
		{
			m_listDocsOpenOnStart.append(viewManager()->view(i)->getDoc()->url().path());
		}
		stage2 =docManager()->fileCloseAll();
	}

	return stage1 && stage2;
}

void Kile::showDocInfo(Kate::Document *doc)
{
	if (doc == 0)
	{
		Kate::View *view = viewManager()->currentView();

		if (view) doc = view->getDoc();
		else return;
	}

	KileDocument::Info *docinfo = docManager()->infoFor(doc);

	if (docinfo)
	{
		KileDocInfoDlg *dlg = new KileDocInfoDlg(docinfo, this, 0, i18n("Summary for Document: %1").arg(getShortName(doc)));
		dlg->exec();
	}
	else
		kdWarning() << "There is know KileDocument::Info object belonging to this document!" << endl;
}

void Kile::convertToASCII(Kate::Document *doc)
{
	if (doc == 0)
	{
		Kate::View *view = viewManager()->currentView();

		if (view) doc = view->getDoc();
		else return;
	}

	ConvertIO io(doc);
	ConvertEncToASCII conv = ConvertEncToASCII(doc->encoding(), &io);
	doc->setEncoding("ISO 8859-1");
	conv.convert();
}

void Kile::convertToEnc(Kate::Document *doc)
{
	if (doc == 0)
	{
		Kate::View *view = viewManager()->currentView();

		if (view) doc = view->getDoc();
		else return;
	}

	if (sender())
	{
		ConvertIO io(doc);
		QString name = QString(sender()->name()).section('_', -1);
		ConvertASCIIToEnc conv = ConvertASCIIToEnc(name, &io);
		conv.convert();
		doc->setEncoding(ConvertMap::encodingNameFor(name));
	}
}

////////////////// GENERAL SLOTS //////////////
void Kile::newStatus(const QString & msg)
{
	statusBar()->changeItem(msg,ID_LINE_COLUMN);
}

int Kile::lineNumber()
{
	Kate::View *view = viewManager()->currentView();

	int para = 0;

	if (view)
	{
		para = view->cursorLine();
	}

	return para;
}

void Kile::newCaption()
{
	Kate::View *view = viewManager()->currentView();
	if (view)
	{
		setCaption(i18n("Document: %1").arg(getName(view->getDoc())));
		if (m_outputView->currentPage()->inherits("KileWidget::Konsole")) m_texKonsole->sync();
	}
}

void Kile::GrepItemSelected(const QString &abs_filename, int line)
{
	kdDebug() << "Open file: "
		<< abs_filename << " (" << line << ")" << endl;
	docManager()->fileOpen(KURL::fromPathOrURL(abs_filename));
	setLine(QString::number(line));
}

void Kile::FindInFiles()
{
	static KileGrepDialog *dlg = 0;

	if (dlg != 0) {
		if (!dlg->isVisible())
			dlg->setDirName((docManager()->activeProject() != 0)
				? docManager()->activeProject()->baseURL().path()
				: QDir::home().absPath() + "/");

		dlg->show();
		return;
	}

	dlg = new KileGrepDialog
		((docManager()->activeProject() != 0)
		? docManager()->activeProject()->baseURL().path()
		: QDir::home().absPath() + "/");

	QString filter(SOURCE_EXTENSIONS);
	filter.append(" ");
	filter.append(PACKAGE_EXTENSIONS);
	filter.replace(".", "*.");
	filter.replace(" ", ",");
	filter.append("|");
	filter.append(i18n("TeX Files"));
	filter.append("\n*|");
	filter.append(i18n("All Files"));
	dlg->setFilter(filter);

	dlg->show();

	connect(dlg, SIGNAL(itemSelected(const QString &, int)),
		this, SLOT(GrepItemSelected(const QString &, int)));
}

/////////////////// PART & EDITOR WIDGET //////////
void Kile::ShowEditorWidget()
{
	ResetPart();
	setCentralWidget(topWidgetStack);
	topWidgetStack->show();
	splitter1->show();
	splitter2->show();
	if (showstructview)  Structview->show();
	if (showoutputview)   m_outputView->show();

	Kate::View *view = viewManager()->currentView();
	if (view) view->setFocus();

	newStatus();
	newCaption();
}


void Kile::ResetPart()
{
	kdDebug() << "==Kile::ResetPart()=============================" << endl;
	kdDebug() << "\tcurrent state " << m_currentState << endl;
	kdDebug() << "\twant state " << m_wantState << endl;

	KParts::ReadOnlyPart *part = (KParts::ReadOnlyPart*)partManager->activePart();

	if (part && m_currentState != "Editor")
	{
		kdDebug() << "\tclosing current part" << endl;
		part->closeURL();
		partManager->removePart(part) ;
		topWidgetStack->removeWidget(part->widget());
		delete part;
	}

	m_currentState = "Editor";
	m_wantState = "Editor";
	partManager->setActivePart( 0L);
}

void Kile::ActivePartGUI(KParts::Part * part)
{
	kdDebug() << "==Kile::ActivePartGUI()=============================" << endl;
	kdDebug() << "\tcurrent state " << m_currentState << endl;
	kdDebug() << "\twant state " << m_wantState << endl;

	//save the toolbar state
	if ( m_wantState == "HTMLpreview" || m_wantState == "Viewer" )
	{
		kdDebug() << "\tsaving toolbar status" << endl;
		m_bShowMainTB = m_paShowMainTB->isChecked();
		m_bShowToolsTB = m_paShowToolsTB->isChecked();
		m_bShowBuildTB = m_paShowBuildTB->isChecked();
		m_bShowErrorTB = m_paShowErrorTB->isChecked();
		m_bShowEditTB = m_paShowEditTB->isChecked();
		m_bShowMathTB = m_paShowMathTB->isChecked();
	}

	createGUI(part);
	unplugActionList("list_quickies"); plugActionList("list_quickies", m_listQuickActions);
	unplugActionList("list_compilers"); plugActionList("list_compilers", m_listCompilerActions);
	unplugActionList("list_converters"); plugActionList("list_converters", m_listConverterActions);
	unplugActionList("list_viewers"); plugActionList("list_viewers", m_listViewerActions);
	unplugActionList("list_other"); plugActionList("list_other", m_listOtherActions);

	showToolBars(m_wantState);

	KParts::BrowserExtension *ext = KParts::BrowserExtension::childObject(part);
	if (ext && ext->metaObject()->slotNames().contains( "print()" ) ) //part is a BrowserExtension, connect printAction()
	{
// 		kdDebug() << "HAS BrowserExtension + print" << endl;
		connect(m_paPrint, SIGNAL(activated()), ext, SLOT(print()));
		m_paPrint->plug(toolBar("mainToolBar"),3); //plug this action into its default location
		m_paPrint->setEnabled(true);
	}
	else
	{
// 		kdDebug() << "NO BrowserExtension + print" << endl;
		if (m_paPrint->isPlugged(toolBar("mainToolBar")))
			m_paPrint->unplug(toolBar("mainToolBar"));

		m_paPrint->setEnabled(false);
	}

	//set the current state
	m_currentState = m_wantState;
	m_wantState = "Editor";
}

void Kile::showToolBars(const QString & wantState)
{
	if ( wantState == "HTMLpreview" )
	{
// 		kdDebug() << "\tchanged to: HTMLpreview" << endl;
		stateChanged( "HTMLpreview");
		toolBar("mainToolBar")->hide();
		toolBar("toolsToolBar")->hide();
		toolBar("buildToolBar")->hide();
		toolBar("errorToolBar")->hide();
		toolBar("editToolBar")->hide();
		toolBar("mathToolBar")->hide();
		toolBar("extraToolBar")->show();
		enableKileGUI(false);
	}
	else if ( wantState == "Viewer" )
	{
// 		kdDebug() << "\tchanged to: Viewer" << endl;
		stateChanged( "Viewer" );
		toolBar("mainToolBar")->show();
		toolBar("toolsToolBar")->hide();
		toolBar("buildToolBar")->hide();
		toolBar("errorToolBar")->hide();
		toolBar("mathToolBar")->hide();
		toolBar("extraToolBar")->show();
		toolBar("editToolBar")->hide();
		enableKileGUI(false);
	}
	else
	{
// 		kdDebug() << "\tchanged to: Editor" << endl;
		stateChanged( "Editor" );
		m_wantState="Editor";
		topWidgetStack->raiseWidget(0);
		if (m_bShowMainTB) toolBar("mainToolBar")->show();
		if (m_bShowEditTB) toolBar("editToolBar")->show();
		if (m_bShowToolsTB) toolBar("toolsToolBar")->show();
		if (m_bShowBuildTB) toolBar("buildToolBar")->show();
		if (m_bShowErrorTB) toolBar("errorToolBar")->show();
		if (m_bShowMathTB) toolBar("mathToolBar")->show();
		toolBar("extraToolBar")->hide();
		enableKileGUI(true);
	}
}

void Kile::enableKileGUI(bool enable)
{
	int id;
	QString text;
	for (uint i=0; i < menuBar()->count(); i++)
	{
		id = menuBar()->idAt(i);
		text = menuBar()->text(id);
		if (
			text == i18n("&Build") ||
			text == i18n("&Project") ||
			text == i18n("&LaTeX") ||
			text == i18n("&Wizard") ||
			text == i18n("&User") ||
			text == i18n("&Graph") ||
			text == i18n("&Tools")
		)
			menuBar()->setItemEnabled(id, enable);
	}
}

//TODO: move to KileView::Manager
void Kile::prepareForPart(const QString & state)
{
// 	kdDebug() << "==Kile::prepareForPart====================" << endl;
	if ( state == m_currentState ) return;

	ResetPart();

	m_wantState = state;

	//deactivate kateparts
	for (uint i=0; i<viewManager()->views().count(); i++)
	{
		guiFactory()->removeClient(viewManager()->view(i));
		viewManager()->view(i)->setActive(false);
	}
}

void Kile::runTool()
{
// 	kdDebug() << "==Kile::runTool()============" << endl;
	QString name = sender()->name();
	kdDebug() << "\tname: " << name << endl;
	name.replace(QRegExp("^.*tool_"), "");
	kdDebug() << "\ttool: " << name << endl;
	m_manager->run(name);
}

// changed clean dialog with selectable items (dani)

void Kile::CleanAll(KileDocument::Info *docinfo, bool silent)
{
	static QString noactivedoc = i18n("There is no active document or it is not saved.");
	if (docinfo == 0)
	{
		Kate::Document *doc = activeDocument();
		if (doc)
			docinfo = docManager()->infoFor(doc);
		else
		{
			m_logWidget->printMsg(KileTool::Error, noactivedoc, i18n("Clean"));
			return;
		}
	}

	if (docinfo)
	{
		QStringList extlist;
		QStringList templist = QStringList::split(" ", KileConfig::cleanUpFileExtensions());
		QString str;
		QFileInfo file(docinfo->url().path()), fi;
		for (uint i=0; i <  templist.count(); i++)
		{
			str = file.dirPath(true) + "/" + file.baseName(true) + templist[i];
			fi.setFile(str);
			if ( fi.exists() )
				extlist.append(templist[i]);
		}

		str = file.fileName();
		if (!silent &&  (str==i18n("Untitled") || str == "" ) )
		{
			m_logWidget->printMsg(KileTool::Error, noactivedoc, i18n("Clean"));
			return;
		}

		if (!silent && extlist.count() > 0 )
		{
			kdDebug() << "\tnot silent" << endl;
			KileDialog::Clean *dialog = new KileDialog::Clean(this, str, extlist);
			if ( dialog->exec() )
				extlist = dialog->getCleanlist();
			else
			{
				delete dialog;
				return;
			}

			delete dialog;
		}

		if ( extlist.count() == 0 )
		{
			m_logWidget->printMsg(KileTool::Warning, i18n("Nothing to clean for %1").arg(str), i18n("Clean"));
			return;
		}

		m_logWidget->printMsg(KileTool::Info, i18n("cleaning %1 : %2").arg(str).arg(extlist.join(" ")), i18n("Clean"));

		docinfo->cleanTempFiles(extlist);
	}
}

////////////////// STRUCTURE ///////////////////
void Kile::ShowStructure()
{
	showVertPage(1);
}

void Kile::RefreshStructure()
{
	showVertPage(1);
	viewManager()->updateStructure(true);
}


/////////////////////// LATEX TAGS ///////////////////
void Kile::insertTag(const KileAction::TagData& data)
{
	logWidget()->clear();
	outputView()->showPage(logWidget());
	setLogPresent(false);

	logWidget()->append(data.description);

	Kate::View *view = viewManager()->currentView();

	if ( !view ) return;

	view->setFocus();

	editorExtension()->insertTag(data, view);
}

void Kile::insertTag(const QString& tagB, const QString& tagE, int dx, int dy)
{
	insertTag(KileAction::TagData(QString::null,tagB,tagE,dx,dy));
}

void Kile::QuickDocument()
{
	KileDialog::QuickDocument *dlg = new KileDialog::QuickDocument(config, this,"Quick Start",i18n("Quick Start"));

	if ( dlg->exec() )
	{
		if ( !viewManager()->currentView() && ( docManager()->createDocumentWithText(QString::null) == 0L ) )
			return;

		insertTag( dlg->tagData() );
	}
	delete dlg;
}

void Kile::QuickTabular()
{
	if ( !viewManager()->currentView() ) return;
	KileDialog::QuickTabular *dlg = new KileDialog::QuickTabular(config, this,"Tabular", i18n("Tabular"));
	if ( dlg->exec() )
	{
		insertTag(dlg->tagData());
	}
	delete dlg;
}

void Kile::QuickTabbing()
{
	if ( !viewManager()->currentView() ) return;
	KileDialog::QuickTabbing *dlg = new KileDialog::QuickTabbing(config, this,"Tabbing", i18n("Tabbing"));
	if ( dlg->exec() )
	{
		insertTag(dlg->tagData());
	}
	delete dlg;
}

void Kile::QuickArray()
{
	if ( !viewManager()->currentView() ) return;
	KileDialog::QuickArray *dlg = new KileDialog::QuickArray(config, this,"Array", i18n("Array"));
	if ( dlg->exec() )
	{
		insertTag(dlg->tagData());
	}
	delete dlg;
}

//////////////////////////// MATHS TAGS/////////////////////////////////////
void Kile::insertSymbol(QIconViewItem *item)
{
	QString code_symbol= item->key();
	insertTag(code_symbol,QString::null,code_symbol.length(),0);
}

void Kile::InsertMetaPost(QListBoxItem *)
{
	QString mpcode=mpview->currentText();
	if (mpcode!="----------") insertTag(mpcode,QString::null,mpcode.length(),0);
}

//////////////// HELP /////////////////
void Kile::LatexHelp()
{
	QString loc = locate("html","en/kile/latexhelp.html");
	KileTool::ViewHTML *tool = dynamic_cast<KileTool::ViewHTML*>(m_toolFactory->create("ViewHTML"));
	tool->setSource(loc);
	tool->setRelativeBaseDir("");
	tool->setTarget("latexhelp.html");
	m_manager->run(tool);
}

///////////////////// USER ///////////////
void Kile::EditUserMenu()
{
	KileDialog::UserTags *dlg = new KileDialog::UserTags(m_listUserTags, this, "Edit User Tags", i18n("Edit User Tags"));

	if ( dlg->exec() )
	{
		//remove all actions
		uint len = m_listUserTagsActions.count();
		for (uint j=0; j< len; j++)
		{
			KAction *menuItem = m_listUserTagsActions.getLast();
			m_menuUserTags->remove(menuItem);
			m_listUserTagsActions.removeLast();
			delete menuItem;
		}
		m_menuUserTags->remove(m_actionEditTag);

		m_listUserTags = dlg->result();
		setupUserTagActions();
	}

	delete dlg;
}

/////////////// CONFIG ////////////////////
void Kile::ReadSettings()
{
	//test for old kilerc
	int version = KileConfig::rCVersion();
	bool old=false;

	m_bShowUserMovedMessage = (version < 4);

	//if the kilerc file is old some of the configuration
	//date must be set by kile, even if the keys are present
	//in the kilerc file
	if ( version < KILERC_VERSION ) old=true;

	if ( version < 4 )
	{
		KileTool::Factory *factory = new KileTool::Factory(0,config);
		kdDebug() << "WRITING STD TOOL CONFIG" << endl;
		factory->writeStdConfig();
	}

	//delete old editor key
	if (config->hasGroup("Editor") )
	{
		config->deleteGroup("Editor");
	}

	config->setGroup( "User" );
	int len = config->readNumEntry("nUserTags",0);
	for (int i = 0; i < len; i++)
	{
		m_listUserTags.append(KileDialog::UserTags::splitTag(config->readEntry("userTagName"+QString::number(i),i18n("no name")) , config->readEntry("userTag"+QString::number(i),"") ));
	}

	//convert user tools to new KileTool classes
	userItem tempItem;
	len= config->readNumEntry("nUserTools",0);
	for (int i=0; i< len; i++)
	{
		tempItem.name=config->readEntry("userToolName"+QString::number(i),i18n("no name"));
		tempItem.tag =config->readEntry("userTool"+QString::number(i),"");
		m_listUserTools.append(tempItem);
	}
	if ( len > 0 )
	{
 		//move the tools
		config->writeEntry("nUserTools", 0);
		for ( int i = 0; i < len; i++)
		{
			tempItem = m_listUserTools[i];
			config->setGroup("Tools");
			config->writeEntry(tempItem.name, "Default");

			KileTool::setGUIOptions(tempItem.name, "Other", "gear", config);

			config->setGroup(KileTool::groupFor(tempItem.name, "Default"));
			QString bin = KRun::binaryName(tempItem.tag, false);
			config->writeEntry("command", bin);
			config->writeEntry("options", tempItem.tag.mid(bin.length()));
			config->writeEntry("class", "Base");
			config->writeEntry("type", "Process");
			config->writeEntry("from", "");
			config->writeEntry("to", "");

			if ( i < 10 )
			{
				config->setGroup("Shortcuts");
				config->writeEntry("tool_" + tempItem.name, "Alt+Shift+" + QString::number(i + 1) ); //should be alt+shift+
			}
		}
	}

	//convert old config names containing spaces to KConfig XT compliant names
	if((0 != version) && (version < 5))
	{
		KxtRcConverter cvt(config, KILERC_VERSION);
		cvt.Convert();
	}

	//reads options that can be set in the configuration dialog
	readConfig();

	//now read the other config data
	m_singlemode=true;
	QRect screen = QApplication::desktop()->screenGeometry();
	resize(KileConfig::mainwindowWidth(), KileConfig::mainwindowHeight());
	split1_left = KileConfig::splitter1_left();
	split1_right = KileConfig::splitter1_right();
	split2_top = KileConfig::splitter2_top();
	split2_bottom = KileConfig::splitter2_bottom();

	showoutputview = KileConfig::outputview();
	showstructview = KileConfig::structureview();

	struct_level1 = KileConfig::structureLevel1();
	struct_level2 = KileConfig::structureLevel2();
	struct_level3 = KileConfig::structureLevel3();
	struct_level4 = KileConfig::structureLevel4();
	struct_level5 = KileConfig::structureLevel5();

	lastvtab = KileConfig::selectedLeftView();
}

void Kile::ReadRecentFileSettings()
{
	config->setGroup("FilesOpenOnStart");
	int n = config->readNumEntry("NoDOOS", 0);
	for (int i=0; i < n; i++)
		m_listDocsOpenOnStart.append(config->readPathEntry("DocsOpenOnStart"+QString::number(i), ""));

	n = config->readNumEntry("NoPOOS", 0);
	for (int i=0; i < n; i++)
		m_listProjectsOpenOnStart.append(config->readPathEntry("ProjectsOpenOnStart"+QString::number(i), ""));

	lastDocument = KileConfig::lastDocument();
	input_encoding = KileConfig::inputEncoding();

	// Load recent files from "Recent Files" group
	// using the KDE standard action for recent files
	fileOpenRecentAction->loadEntries(config,"Recent Files");

	// Now check if user is using an old rc file that has "Recent Files" under
	// the "Files" group
	if(config->hasKey("Recent Files"))
	{
		// If so, then read the entry in, add it to fileOpenRecentAction
		QStringList recentFilesList = config->readListEntry("Recent Files", ':');
		QStringList::ConstIterator it = recentFilesList.begin();
		for ( ; it != recentFilesList.end(); ++it )
		{
		fileOpenRecentAction->addURL(KURL::fromPathOrURL(*it));
		}
		// Now delete this recent files entry as we are now using a separate
		// group for recent files
		config->deleteEntry("Recent Files");
	}

	m_actRecentProjects->loadEntries(config,"Projects");
}

//reads options that can be set in the configuration dialog
void Kile::readConfig()
{
// 	kdDebug() << "==Kile::readConfig()=======================" << endl;

	m_kwStructure->setLevel(KileConfig::defaultLevel());

	m_bRestore = KileConfig::restore();
	autosave = KileConfig::autosave();
	autosaveinterval = KileConfig::autosaveInterval();
	enableAutosave(autosave);
	setAutosaveInterval(autosaveinterval);

	m_templAuthor = KileConfig::author();
	m_templDocClassOpt = KileConfig::documentClassOptions();
	m_templEncoding = KileConfig::templateEncoding();

	m_runlyxserver = KileConfig::runLyxServer();

//////////////////// code completion (dani) ////////////////////
	m_edit->complete()->readConfig();
}

void Kile::SaveSettings()
{
	ShowEditorWidget();

	KileFS->writeConfig();

	KileConfig::setLastDocument(lastDocument);
	input_encoding=KileFS->comboEncoding->lineEdit()->text();
	KileConfig::setInputEncoding(input_encoding);

	// Store recent files
	fileOpenRecentAction->saveEntries(config,"Recent Files");
	m_actRecentProjects->saveEntries(config,"Projects");

	config->deleteGroup("FilesOpenOnStart");
	kdDebug() << "deleting FilesOpenOnStart" << endl;
	if (m_bRestore)
	{
		kdDebug() << "saving Restore info" << endl;
		config->setGroup("FilesOpenOnStart");
		config->writeEntry("NoDOOS", m_listDocsOpenOnStart.count());
		for (uint i=0; i < m_listDocsOpenOnStart.count(); i++)
			config->writePathEntry("DocsOpenOnStart"+QString::number(i), m_listDocsOpenOnStart[i]);

		config->writeEntry("NoPOOS", m_listProjectsOpenOnStart.count());
		for (uint i=0; i < m_listProjectsOpenOnStart.count(); i++)
			config->writePathEntry("ProjectsOpenOnStart"+QString::number(i), m_listProjectsOpenOnStart[i]);

		if (!m_singlemode)
			KileConfig::setMaster(m_masterName);
		else
			KileConfig::setMaster("");
	}

	config->setGroup( "User" );

	config->writeEntry("nUserTags",static_cast<int>(m_listUserTags.size()));
	for (uint i=0; i < m_listUserTags.size(); i++)
	{
		KileAction::TagData td( m_listUserTags[i]);
		config->writeEntry( "userTagName"+QString::number(i),  td.text );
		config->writeEntry( "userTag"+QString::number(i), KileDialog::UserTags::completeTag(td) );
	}

	actionCollection()->writeShortcutSettings();
	saveMainWindowSettings(config, "KileMainWindow" );

	KileConfig::setRCVersion(KILERC_VERSION);
	QValueList<int> sizes;
	QValueList<int>::Iterator it;
	KileConfig::setMainwindowWidth(width());
	KileConfig::setMainwindowHeight(height());
	sizes=splitter1->sizes();
	it = sizes.begin();
	split1_left=*it;
	++it;
	split1_right=*it;
	sizes.clear();
	sizes=splitter2->sizes();
	it = sizes.begin();
	split2_top=*it;
	++it;
	split2_bottom=*it;

	KileConfig::setSplitter1_left(split1_left);
	KileConfig::setSplitter1_right(split1_right);
	KileConfig::setSplitter2_top(split2_top);
	KileConfig::setSplitter2_bottom(split2_bottom);

	KileConfig::setOutputview(showoutputview);
	KileConfig::setStructureview(showstructview);

	KileConfig::setStructureLevel1(struct_level1);
	KileConfig::setStructureLevel2(struct_level2);
	KileConfig::setStructureLevel3(struct_level3);
	KileConfig::setStructureLevel4(struct_level4);
	KileConfig::setStructureLevel5(struct_level5);

	KileConfig::setSelectedLeftView(ButtonBar->getRaisedTab());

	KileConfig::writeConfig();
	config->sync();
}

/////////////////  OPTIONS ////////////////////
void Kile::ToggleMode()
{
	if (!m_singlemode)
	{
		ModeAction->setText(i18n("Define Current Document as 'Master Document'"));
		ModeAction->setChecked(false);
		m_logPresent=false;
		m_singlemode=true;
	}
	else if (m_singlemode && viewManager()->currentView())
	{
		m_masterName=getName();
		if (m_masterName==i18n("Untitled") || m_masterName=="")
		{
			ModeAction->setChecked(false);
			KMessageBox::error(this, i18n("In order to define the current document as a master document, it has to be saved first."));
			m_masterName="";
			return;
		}

		QString shortName = m_masterName;
		int pos;
		while ( (pos = (int)shortName.find('/')) != -1 )
		shortName.remove(0,pos+1);
		ModeAction->setText(i18n("Normal mode (current master document: %1)").arg(shortName));
		ModeAction->setChecked(true);
		m_singlemode=false;
	}
	else
		ModeAction->setChecked(false);

	updateModeStatus();
}

void Kile::ToggleOutputView()
{
	ShowOutputView(true);
}

void Kile::ToggleStructView()
{
	ShowStructView(true);
}

void Kile::ToggleWatchFile()
{
	m_bWatchFile=!m_bWatchFile;

	if (m_bWatchFile)
		WatchFileAction->setChecked(true);
	else
		WatchFileAction->setChecked(false);
}

void Kile::ShowOutputView(bool change)
{
	if (change) showoutputview=!showoutputview;
	if (showoutputview)
	{
		MessageAction->setChecked(true);
		m_outputView->show();
	}
	else
	{
		MessageAction->setChecked(false);
		m_outputView->hide();
	}
}

void Kile::ShowStructView(bool change)
{
	if (change) showstructview=!showstructview;

	if (showstructview)
	{
		StructureAction->setChecked(true);
		Structview->show();
	}
	else
	{
		StructureAction->setChecked(false);
		Structview->hide();
	}
}

void Kile::GeneralOptions()
{
	KileDialog::Config *dlg = new KileDialog::Config(config, m_manager, this);

	if (dlg->exec())
	{
		readConfig();
		setupTools();

		emit(configChanged());

		//stop/restart LyX server if necessary
		if (m_runlyxserver && !m_lyxserver->isRunning())
			m_lyxserver->start();

		if (!m_runlyxserver && m_lyxserver->isRunning())
			m_lyxserver->stop();
	}

	delete dlg;
}

void Kile::slotPerformCheck()
{
	KileDialog::ConfigChecker *dlg = new KileDialog::ConfigChecker(this);
	dlg->exec();
	delete dlg;
}

/////////////// KEYS - TOOLBARS CONFIGURATION ////////////////
void Kile::ConfigureKeys()
{
	KKeyDialog dlg( false, this );
	QPtrList<KXMLGUIClient> clients = guiFactory()->clients();
	for( QPtrListIterator<KXMLGUIClient> it( clients );	it.current(); ++it )
	{
		dlg.insert( (*it)->actionCollection() );
	}
	dlg.configure();
	actionCollection()->writeShortcutSettings("Shortcuts", config);
}

void Kile::ConfigureToolbars()
{
	saveMainWindowSettings(config, "KileMainWindow" );
	KEditToolbar dlg(factory());
	dlg.exec();

	showToolBars(m_currentState);
}

////////////// VERTICAL TAB /////////////////
void Kile::showVertPage(int page)
{
	ButtonBar->setTab(lastvtab,false);
	ButtonBar->setTab(page,true);
	lastvtab=page;

	if (page==0)
	{
		viewManager()->projectView()->hide();
		m_kwStructure->hide();
		mpview->hide();
		if (symbol_view && symbol_present) delete symbol_view;
		symbol_present=false;
		if (Structview_layout) delete Structview_layout;
		Structview_layout=new QHBoxLayout(Structview);
		Structview_layout->add(KileFS);
		Structview_layout->add(ButtonBar);
		ButtonBar->setPosition(KMultiVertTabBar::Right);
		KileFS->show();
	}
	else if (page==1)
	{
		//UpdateStructure();
		viewManager()->projectView()->hide();
		KileFS->hide();
		mpview->hide();
		if (symbol_view && symbol_present) delete symbol_view;
		symbol_present=false;
		if (Structview_layout) delete Structview_layout;
		Structview_layout=new QHBoxLayout(Structview);
		Structview_layout->add(m_kwStructure);
		Structview_layout->add(ButtonBar);
		ButtonBar->setPosition(KMultiVertTabBar::Right);
		m_kwStructure->show();
	}
	else if (page==8)
	{
		viewManager()->projectView()->hide();
		KileFS->hide();
		m_kwStructure->hide();
		if (symbol_view && symbol_present) delete symbol_view;
		symbol_present=false;
		if (Structview_layout) delete Structview_layout;
		Structview_layout=new QHBoxLayout(Structview);
		Structview_layout->add(mpview);
		Structview_layout->add(ButtonBar);
		ButtonBar->setPosition(KMultiVertTabBar::Right);
		mpview->show();
	}
	else if (page==9)
	{
		kdDebug() << "SHOWING PROJECTS VIEW" << endl;
		if (symbol_view && symbol_present) delete symbol_view;
		symbol_present=false;
		KileFS->hide();
		m_kwStructure->hide();
		mpview->hide();
		delete Structview_layout;
		Structview_layout=new QHBoxLayout(Structview);
		Structview_layout->add(viewManager()->projectView());
		Structview_layout->add(ButtonBar);
		ButtonBar->setPosition(KMultiVertTabBar::Right);
		viewManager()->projectView()->show();
	}
	else
	{
		viewManager()->projectView()->hide();
		KileFS->hide();
		m_kwStructure->hide();
		mpview->hide();
		if (symbol_view && symbol_present) delete symbol_view;
		if (Structview_layout) delete Structview_layout;
		Structview_layout=new QHBoxLayout(Structview);
		symbol_view = new SymbolView(page-1,Structview,"Symbols");
		connect(symbol_view, SIGNAL(executed(QIconViewItem*)), SLOT(insertSymbol(QIconViewItem*)));
		symbol_present=true;
		Structview_layout->add(symbol_view);
		Structview_layout->add(ButtonBar);
		ButtonBar->setPosition(KMultiVertTabBar::Right);
		symbol_view->show();
	}
}

void Kile::changeInputEncoding()
{
	Kate::View *view = viewManager()->currentView();
	if (view)
	{
		bool modified = view->getDoc()->isModified();

  		QString encoding=KileFS->comboEncoding->lineEdit()->text();
		QString text = view->getDoc()->text();

		view->getDoc()->setEncoding(encoding);
		//reload the document so that the new encoding takes effect
		view->getDoc()->openURL(view->getDoc()->url());

		docManager()->setHighlightMode(view->getDoc());
		view->getDoc()->setModified(modified);
	}
}


//////////////////// CLEAN BIB /////////////////////
void Kile::CleanBib()
{
	Kate::View *view = viewManager()->currentView();
	if ( ! view )
		return;

	uint i=0;
	QString s;

	while(i < view->getDoc()->numLines())
	{
		s = view->getDoc()->textLine(i);
		s=s.left(3);
		if (s=="OPT" || s=="ALT")
		{
			view->getDoc()->removeLine(i );
			view->getDoc()->setModified(true);
		}
		else
			i++;
	}
}

void Kile::includeGraphics()
{
	Kate::View *view = viewManager()->currentView();
	if ( !view ) return;

	QFileInfo fi( view->getDoc()->url().path() );
	KileDialog::IncludeGraphics *dialog = new KileDialog::IncludeGraphics(this, fi.dirPath(), false);

	if ( dialog->exec() == QDialog::Accepted )
		insertTag( dialog->getTemplate(),"%C",0,0 );

	delete dialog;
}

void Kile::slotToggleFullScreen()
{
	m_bFullScreen = !m_bFullScreen;
	if( m_bFullScreen )
	{
		this->showFullScreen();
		m_pFullScreen->setText( i18n( "Exit Full-Screen Mode" ) );
		m_pFullScreen->setToolTip( i18n( "Exit full-screen mode" ) );
		m_pFullScreen->setIcon( "window_nofullscreen" );
	}
	else 
	{
		this->showNormal();
		m_pFullScreen->setText( i18n( "&Full-Screen Mode" ) );
		m_pFullScreen->setToolTip(i18n("Full-screen mode"));
		m_pFullScreen->setIcon( "window_fullscreen" );
	}
}

#include "kile.moc"
