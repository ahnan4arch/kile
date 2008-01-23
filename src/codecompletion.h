/************************************************************************************************
    date                 : Mar 21 2007
    version              : 0.40
    copyright            : (C) 2004-2007 by Holger Danielsson (holger.danielsson@versanet.de)
                               2008 by Michel Ludwig (michel.ludwig@kdemail.net)
 ************************************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef CODECOMPLETION_H
#define CODECOMPLETION_H

#include <QObject>
#include <QList>

#include <KTextEditor/CodeCompletionInterface>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <kconfig.h>

#include "latexcmd.h"
#include "widgets/abbreviationview.h"

namespace KTextEditor {class CompletionEntry;}

//default bullet char (a cross)
static const QChar s_bullet_char = QChar(0xd7);
static const QString s_bullet = QString(&s_bullet_char, 1);
		
/**
  *@author Holger Danielsson
  */
  
class QTimer;

class KileInfo;

namespace KileDocument
{

//FIXME fix the way the KTextEditor::View is passed, I'm not 100% confident m_view doesn't turn into a wild pointer
//FIXME refactor the complete class, it's pretty ugly, there are too many methods with similar names suggesting that the code could be more efficient
class CodeCompletion : public QObject
{
	Q_OBJECT

public:
	CodeCompletion(KileInfo *ki);
	~CodeCompletion();

	enum Mode
	{
		cmLatex,
		cmEnvironment,
		cmDictionary,
		cmAbbreviation,
		cmLabel,
		cmDocumentWord
	};

	enum Type
	{
		ctNone,
		ctReference,
		ctCitation
	};

	enum KateConfigFlags
	{
		cfAutoIndent = 0x1,
		cfAutoBrackets = 0x40
	};

	bool isActive();
	bool inProgress();
	bool autoComplete();
	CodeCompletion::Mode getMode();
	CodeCompletion::Type getType();
	CodeCompletion::Type getType(const QString &text);

	KileInfo* info() const { return m_ki;}

	void readConfig(KConfig *config);
	void readKateConfigFlags(KConfig *config);
	void saveLocalChanges();

	void setAbbreviationListview(KileWidget::AbbreviationView *listview);

public Q_SLOTS:
	//in these two methods we should set m_view
	void textInsertedInView(KTextEditor::View *view, const KTextEditor::Cursor &position, const QString &text);
	void editComplete(KTextEditor::View *view, KileDocument::CodeCompletion::Mode mode);

	void slotCompletionDone(KTextEditor::CompletionEntry);
	void slotCompleteValueList();
	void slotCompletionAborted();

	void slotFilterCompletion(KTextEditor::CompletionEntry* c, QString *s);

	// a abbreviation was modified ind the abbreviation view (add, edit or delete)
	// so the abbreviation list was must also be updated
	void slotUpdateAbbrevList(const QString &ds, const QString &as);

private:
	void completeWord(const QString &text, CodeCompletion::Mode mode);
	QString filterCompletionText(const QString &text, const QString &type);

	void CompletionDone(KTextEditor::CompletionEntry);
	void CompletionAborted();

	void completeFromList(const QStringList *list,const QString &pattern = QString());
	void editCompleteList(KileDocument::CodeCompletion::Type type,const QString &pattern = QString());
	bool getCompleteWord(bool latexmode, QString &text, KileDocument::CodeCompletion::Type &type);
	bool getReferenceWord(QString &text);
	bool oddBackslashes(const QString& text, int index);

	void appendNewCommands(QList<KTextEditor::CompletionEntry>& list);
	void getDocumentWords(const QString &text, QList<KTextEditor::CompletionEntry>& list);

	bool completeAutoAbbreviation(const QString &text);
	QString getAbbreviationWord(uint row, uint col);

	CodeCompletion::Type insideReference(QString &startpattern);

private:
	// wordlists
	QList<KTextEditor::CompletionEntry> m_texlist;
	QList<KTextEditor::CompletionEntry> m_dictlist;
	QList<KTextEditor::CompletionEntry> m_abbrevlist;
	QList<KTextEditor::CompletionEntry> m_labellist;

	KileInfo *m_ki;
	QTimer *m_completeTimer;

	// some flags
	bool m_isenabled;
	bool m_setcursor;
	bool m_setbullets;
	bool m_closeenv;
	bool m_autocomplete;
	bool m_autocompletetext;
	bool m_autocompleteabbrev;
	bool m_citationMove;
	bool m_autoDollar;
	int  m_latexthreshold;
	int  m_textthreshold;

	// flags from Kate configuration
	bool m_autobrackets;
	bool m_autoindent;

	// state of complete: some flags
	bool m_firstconfig;
	bool m_inprogress;
	bool m_ref;
	bool m_kilecompletion;
	
	// undo text
	bool m_undo;

	// special types: ref, bib
	CodeCompletion::Type m_type;
	CodeCompletion::Type m_keylistType;

	// local abbreviation
	QString m_localAbbrevFile;
	KileWidget::AbbreviationView *m_abbrevListview;

	// internal parameter
	KTextEditor::View *m_view;                  // View
	QString m_text;                      // current pattern
	uint m_textlen;                      // length of current pattern
	CodeCompletion::Mode m_mode;         // completion mode
	uint m_ycursor,m_xcursor,m_xstart;   // current cursor position
	uint m_yoffset,m_xoffset;            // offset of the new cursor position

	QString buildLatexText(const QString &text, int &ypos, int &xpos);
	QString buildEnvironmentText(const QString &text, const QString &type, const QString &prefix, int &ypos, int &xpos);
	QString getWhiteSpace(const QString &s);
	QString buildAbbreviationText(const QString &text);
	QString buildLabelText(const QString &text);

	QString parseText(const QString &text, int &ypos, int &xpos, bool checkgroup);
	QString stripParameter(const QString &text);

	void setWordlist(const QStringList &files,const QString &dir, QList<KTextEditor::CompletionEntry> *entrylist);
	void readWordlist(QStringList &wordlist, const QString &filename, bool global);
	void addCommandsToTexlist(QStringList &wordlist);
	
	void setReferences();
	QString getCommandList(KileDocument::CmdAttribute attrtype);
	
	void setCompletionEntries(QList<KTextEditor::CompletionEntry> *list, const QStringList &wordlist);
	void setCompletionEntriesTexmode(QList<KTextEditor::CompletionEntry> *list, const QStringList &wordlist);

	uint countEntries(const QString &pattern, QList<KTextEditor::CompletionEntry> *list, QString *entry, QString *type);

	void addAbbreviationEntry( const QString &entry );
	void deleteAbbreviationEntry( const QString &entry );
	QString findExpansion(const QString &abbrev);

	void autoInsertDollar();
};

}

#endif
