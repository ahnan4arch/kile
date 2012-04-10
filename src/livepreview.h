/********************************************************************************
  Copyright (C) 2011-2012 by Michel Ludwig (michel.ludwig@kdemail.net)
 ********************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef LIVEPREVIEW_H
#define LIVEPREVIEW_H

#include "documentinfo.h"
#include "kileinfo.h"
#include "kileproject.h"
#include "kiletool.h"
#include "editorextension.h"
#include "widgets/previewwidget.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSignalMapper>
#include <QString>
#include <QStringList>

#include <KLed>
#include <KToggleAction>
#include <KTempDir>

namespace KileDocument { class TextInfo; }

namespace KileTool
{

class LivePreviewManager : public QObject
{
	Q_OBJECT

public:
	// has to be instatiated after the view manager only!
	LivePreviewManager(KileInfo *ki, KActionCollection *ac);
	~LivePreviewManager();

	// live preview won't be run in 'boot up' mode, which is enabled by default
	void disableBootUpMode();

	void readConfig(KConfig *config);
	void writeConfig();
	void readLivePreviewStatusSettings(KConfig *config);
	void writeLivePreviewStatusSettings(KConfig *config);

	void compilePreview(KileDocument::LaTeXInfo *info, KTextEditor::View *view);
	void showPreviewCompileIfNecessary(KileDocument::LaTeXInfo *info, KTextEditor::View *view);

	bool isLivePreviewActive() const;
	bool isLivePreviewPossible() const;

	bool isLivePreviewEnabledForCurrentDocument();
	void setLivePreviewEnabledForCurrentDocument(bool b);

	QWidget* getControlToolBar();

	void buildLivePreviewMenu(KConfig *config);

public Q_SLOTS:
	void handleCursorPositionChanged(KTextEditor::View *view, const KTextEditor::Cursor &pos);
	void handleTextChanged(KTextEditor::Document *doc);

	void handleMasterDocumentChanged();

	void refreshLivePreview();

private Q_SLOTS:
	void recompileLivePreview();

	void handleDocumentModificationTimerTimeout();

	// TextInfo* object due to the signal 'aboutToBeDestroyed(KileDocument::TextInfo*)'
	void removeLaTeXInfo(KileDocument::TextInfo *info);
	void removeProject(KileProject *project);

	void toolDestroyed();
	void toolDone(KileTool::Base *base, int i, bool childToolSpawned);
	void childToolDone(KileTool::Base *base, int i, bool childToolSpawned);

	void handleTextViewActivated(KTextEditor::View *view, bool clearPreview = true, bool forceCompilation = false);
	void handleTextViewClosed(KTextEditor::View *view, bool wasActiveView);

	void handleProjectItemAdded(KileProject *project, KileProjectItem *item);
	void handleProjectItemRemoved(KileProject *project, KileProjectItem *item);

	void handleDocumentSavedAs(KTextEditor::View*, KileDocument::TextInfo*);

	void handleSpawnedChildTool(KileTool::Base *parent, KileTool::Base *child);

	void synchronizeViewWithCursorActionTriggered(bool b);
	void previewForCurrentDocumentActionTriggered(bool b);

	void livePreviewToolActionTriggered();

	void handleCursorPositionChangedTimeout();

private:
	class PreviewInformation;

	KileInfo *m_ki;
	bool m_bootUpMode;
	QPointer<KToolBar> m_controlToolBar;
	QPointer<KLed> m_previewStatusLed;
	KToggleAction *m_synchronizeViewWithCursorAction, *m_previewForCurrentDocumentAction;
	QTimer *m_ledBlinkingTimer, *m_documentChangedTimer, *m_cursorPositionChangedTimer;

	QHash<QString, QString> m_runningPathToPreviewPathHash;
	QHash<QString, QString> m_runningPreviewPathToPathHash;
	QString m_runningPreviewFile;
	KileDocument::LaTeXInfo *m_runningLaTeXInfo;
	KTextEditor::View *m_runningTextView;
	KileProject *m_runningProject;
	PreviewInformation *m_runningPreviewInformation;
	QHash<KileDocument::TextInfo*, QByteArray> m_runningTextHash;

	PreviewInformation *m_shownPreviewInformation;

	QHash<KileDocument::LaTeXInfo*, PreviewInformation*> m_latexInfoToPreviewInformationHash;
	QHash<KileProject*, PreviewInformation*> m_projectToPreviewInformationHash;
	PreviewInformation *m_masterDocumentPreviewInformation;

	// all the members required to handle tool actions for live preview
	QHash<ToolConfigPair, KAction*> m_livePreviewToolToActionHash;
	QHash<KAction*, ToolConfigPair> m_actionToLivePreviewToolHash;
	QActionGroup *m_livePreviewToolActionGroup;
	QLinkedList<KAction*> m_livePreviewToolActionList;

	PreviewInformation* findPreviewInformation(KileDocument::TextInfo *textInfo, KileProject* *locatedProject = NULL,
	                                                                               LivePreviewUserStatusHandler* *userStatusHandler = NULL);

	void updatePreviewInformationAfterCompilationFinished();

	void displayErrorMessage(const QString &text, bool clearFirst = false);

	void createActions(KActionCollection *ac);
	void createControlToolBar();
	void synchronizeViewWithCursor(KileDocument::TextInfo *info, KTextEditor::View *view, const KTextEditor::Cursor& newPosition);

	void stopAndClearPreview();

	void showPreviewDisabled();
	void showPreviewRunning();
	void showPreviewFailed();
	void showPreviewSuccessful();
	void showPreviewOutOfDate();

	void stopLivePreview();
	void clearLivePreview();

	void deleteAllLivePreviewInformation();

	void handleProjectItemAdditionOrRemoval(KileProject *project, KileProjectItem *item);

	void fillTextHashForMasterDocument(QHash<KileDocument::TextInfo*, QByteArray> &textHash);

	void disablePreview();

	void updateLivePreviewToolActions(LivePreviewUserStatusHandler *statusHandler);
	void setLivePreviewToolActionsEnabled(bool b);
};

}

#endif
