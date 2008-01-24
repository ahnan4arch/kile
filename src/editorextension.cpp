/***********************************************************************************************
    date                 : Mar 12 2007
    version              : 0.46
    copyright            : (C) 2004-2007 by Holger Danielsson (holger.danielsson@versanet.de)
                               2008 by Michel Ludwig (michel.ludwig@kdemail.net)
 ***********************************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "editorextension.h"

#include <QFileInfo>
#include <QClipboard>

#include <KApplication>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <KTextEditor/SearchInterface>
#include <KLocale>
#include <KStandardDirs>

#include "widgets/konsolewidget.h"
#include "kileinfo.h"
#include "kileviewmanager.h"
#include "kileconfig.h"
#include "kileactions.h"

#include "kiletool_enums.h"
#include "widgets/logwidget.h"
#include "kileextensions.h"
#include "quickpreview.h"

namespace KileDocument
{

EditorExtension::EditorExtension(KileInfo *info) : m_ki(info)
{
	m_complete = new KileDocument::CodeCompletion(m_ki);
	m_latexCommands = m_ki->latexCommands();

	// init regexp
	m_reg.setPattern("(\\\\(begin|end)\\s*\\{([A-Za-z]+\\*?)\\})|(\\\\\\[|\\\\\\])");
	//                1    2                 3                   4         
	m_regexpEnter.setPattern("^(.*)((\\\\begin\\s*\\{([^\\{\\}]*)\\})|(\\\\\\[))");
	//                         1   23                 4               5 

	// init double quotes
	m_quoteList 
		<< "English quotes:   ``   ''"
		<< "French quotes:   \"<   \">"
		<< "German quotes:   \"`   \"'"
		<< "French quotes (long):   \\flqq   \\frqq"
		<< "German quotes (long):   \\glqq   \\grqq"
		<< "Icelandic quotes (v1):   \\ilqq   \\irqq"
		<< "Icelandic quotes (v2):   \\iflqq   \\ifrqq"
		<< "Czech quotes:   \\uv{   }"
		;

	readConfig();
}

EditorExtension::~EditorExtension()
{
	delete m_complete;
}

//////////////////// read configuration ////////////////////

void EditorExtension::readConfig(void)
{
	// init insertion of double quotes
	initDoubleQuotes();

	// calculate indent for autoindent of environments
	m_envAutoIndent.clear();
	if(KileConfig::envIndentation()) {
		if(KileConfig::envIndentSpaces()) {
			int num = KileConfig::envIndentNumSpaces();
			if(num < 1 || num > 9) {
				num = 1;
			}
			m_envAutoIndent.fill(' ', num);
		}
		else {
			m_envAutoIndent = "\t";
		}
	}
}

void EditorExtension::insertTag(const KileAction::TagData& data, KTextEditor::View *view)
{
	KTextEditor::Document *doc = view->document();
	if(!doc) {
		return;
	}

	//whether or not to wrap tag around selection
	bool wrap = !data.tagEnd.isNull() && view->selection();

	//%C before or after the selection
	bool before = data.tagBegin.count("%C");
	bool after = data.tagEnd.count("%C");

	//save current cursor position
	KTextEditor::Cursor cursor = view->cursorPosition();
	KTextEditor::Cursor virtualCursor = view->cursorPositionVirtual();
	int para = cursor.line();
	int para_begin = para;
	int index = virtualCursor.column();
	int index_begin = index;
	int para_end = 0;
	int index_cursor = index;
	int para_cursor = index;
	// offset for autoindentation of environments
	int dxIndentEnv = 0;

	// environment tag
	bool envtag = data.tagBegin.count("%E") || data.tagEnd.count("%E");
	QString whitespace = getWhiteSpace( doc->line(para).left(index) );

	//if there is a selection act as if cursor is at the beginning of selection
	if (wrap) {
		KTextEditor::Range selectionRange = view->selectionRange();
		index = selectionRange.start().column();
		para  = selectionRange.start().line();
		para_end = selectionRange.end().line();
	}

	QString ins = data.tagBegin;
	QString tagEnd = data.tagEnd;

	//start an atomic editing sequence
	doc->startEditing();

	//cut the selected text
	QString trailing;
	if(wrap) {
		QString sel = view->selectionText();
		view->removeSelectionText();
		
		// no autoindentation of environments, when text is selected
		if(envtag) {
			ins.remove("%E");
			tagEnd.remove("%E\n");
		}

		// strip one of two consecutive line ends
		int len = sel.length();
		if(tagEnd.at(0)=='\n' && len > 0 && sel.indexOf('\n',-1) == len - 1) {
			sel.truncate( len-1 );
		}

		// now add the selection
		ins += sel;
		
		// place the cursor behind this tag, if there is no other wish
		if(!before && !after) {
			trailing = "%C";
			after = true;
		}
	}
	else if(envtag) {
		ins.replace("%E",whitespace+m_envAutoIndent);
		tagEnd.replace("%E",whitespace+m_envAutoIndent);
		if(data.dy > 0) {
			dxIndentEnv = whitespace.length() + m_envAutoIndent.length();
		}
	}

	tagEnd.replace("\\end{",whitespace+"\\end{");
	ins += tagEnd + trailing;

	//do some replacements
	QFileInfo fi( doc->url().path());
	ins.replace("%S", fi.baseName(true));
	ins.replace("%B", s_bullet);
	
	//insert first part of tag at cursor position
	doc->insertText(KTextEditor::Cursor(para, index), ins);

	//move cursor to the new position
	if(before || after) {
		int n = data.tagBegin.count("\n")+ data.tagEnd.count("\n");
		if(wrap) {
			n += para_end > para ? para_end-para : para-para_end;
		}
		for (int line = para_begin; line <= para_begin+n; ++line) {
			if(doc->line(line).count("%C")) {
				int i=doc->line(line).indexOf("%C");
				para_cursor = line; index_cursor = i;
				doc->removeText(KTextEditor::Range(line, i, line, i+2));
				break;
			}
			index_cursor=index;
			para_cursor=line;
		}
	}
	else {
		int py = para_begin, px = index_begin;
		if(wrap) { //act as if cursor was at beginning of selected text (which is the point where the tagBegin is inserted)
			py = para;
			px = index;
		}
		para_cursor = py+data.dy; index_cursor = px+data.dx+dxIndentEnv;
	}

	//end the atomic editing sequence
	doc->endEditing();

	//set the cursor position (it is important that this is done outside of the atomic editing sequence)
	view->setCursorPosition(KTextEditor::Cursor(para_cursor, index_cursor));

	view->removeSelection();
}

//////////////////// goto environment tag (begin or end) ////////////////////

// goto the next non-nested environment tag

KTextEditor::View* EditorExtension::determineView(KTextEditor::View *view)
{
	if (!view) {
		view = m_ki->viewManager()->currentTextView();
	}

	m_overwritemode = (!view) ? false : (view->viewEditMode() == KTextEditor::View::EditOverwrite);

	return view;
}

void EditorExtension::gotoEnvironment(bool backwards, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	uint row,col;
	EnvData env;
	bool found;
	
	// get current position
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	
	// start searching
	if(backwards) {
		found = findBeginEnvironment(doc,row,col,env);
		//KILE_DEBUG() << "   goto begin env:  " << env.row << "/" << env.col;
	
	}
	else {
		found = findEndEnvironment(doc,row,col,env);
		if(!m_overwritemode) {
			env.col += env.len;
		}
	}
	
	if(found) {
		view->setCursorPosition(KTextEditor::Cursor(env.row, env.col));
	}
}

// match the opposite environment tag

void EditorExtension::matchEnvironment(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	uint row,col;
	EnvData env;
	
	// get current position
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	// we only start, when we are at an environment tag
	if(!isEnvironmentPosition(doc, row, col, env)) {
		return;
	}
	
	gotoEnvironment( env.tag != EnvBegin,view);
}

//////////////////// close opened environments  ////////////////////

// search for the last opened environment and close it

void EditorExtension::closeEnvironment(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int row, col, currentRow, currentCol;
	QString name;
	
	KTextEditor::Cursor cursor = view->cursorPosition();
	currentRow = cursor.line();
	currentCol = cursor.column();

	if(findOpenedEnvironment(row, col, name, view)) {
		if(name == "\\[") {
			view->document()->insertText(KTextEditor::Cursor(currentRow, currentCol), "\\]");
		}
		else {
			view->document()->insertText(KTextEditor::Cursor(currentRow, currentCol), "\\end{" + name + '}');
		}
// 		view->setCursorPosition(KTextEditor::Cursor(row + 1, 0));
	}
}

// close all opened environments

void EditorExtension::closeAllEnvironments(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	QStringList envlist = findOpenedEnvironmentList(view, true);
	if(envlist.count() == 0) {
		return;
	}

	int currentRow, currentCol, outputCol;
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	currentRow = cursor.line();
	currentCol = cursor.column();

	bool indent = !m_envAutoIndent.isEmpty();
	if(indent && currentCol > 0) {
		doc->insertText(KTextEditor::Cursor(currentRow, currentCol),"\n");
		currentRow++;
		currentCol = 0;
	}
		
	bool ok1,ok2;
	for(QStringList::Iterator it = envlist.begin(); it != envlist.end(); ++it) {
		QStringList entry = (*it).split(',');
		if(entry[0] == "document") {
			break;
		}

		int row = entry[1].toInt(&ok1);
		int col = entry[2].toInt(&ok2);
		if(!ok1 || !ok2) {
			continue;
		}

		outputCol = currentCol;
		if(indent) {
			QString whitespace = getWhiteSpace( doc->line(row).left(col) );
			doc->insertText(KTextEditor::Cursor(currentRow, outputCol), whitespace);
			outputCol += whitespace.length();
		}
		QString endtag = ( entry[0] == "\\[" ) ? "\\]\n" : "\\end{"+entry[0]+"}\n";
		doc->insertText(KTextEditor::Cursor(currentRow, outputCol), endtag);
		++currentRow;
	}
}

//////////////////// mathgroup ////////////////////

void EditorExtension::selectMathgroup(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int row1, col1, row2, col2;
	if(getMathgroup(view, row1, col1, row2, col2)) {
		view->setSelection(KTextEditor::Range(row1, col1, row2, col2));
	}
}

void EditorExtension::deleteMathgroup(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int row1, col1, row2, col2;
	if(getMathgroup(view, row1, col1, row2, col2)) {
		view->removeSelection();
		view->document()->removeText(KTextEditor::Range(row1, col1, row2, col2));
		view->setCursorPosition(KTextEditor::Cursor(row1, 0));
	}
}

bool EditorExtension::hasMathgroup(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return false;
	}

	int row1, col1, row2, col2;
	return getMathgroup(view, row1, col1, row2, col2);
}

QString EditorExtension::getMathgroupText(uint &row, uint &col, KTextEditor::View *view)
{
	int row1, col1, row2, col2;

	view = determineView(view);
	if(view && getMathgroup(view, row1, col1, row2, col2)) {
		row = row1;
		col = col1;
		return view->document()->text(KTextEditor::Range(row1, col1, row2, col2));
	}
	else {
		return QString();
	}
}


bool EditorExtension::getMathgroup(KTextEditor::View *view, int &row1, int &col1, int &row2, int &col2)
{
	QRegExp reg(QString("\\$")
		+ "|\\\\begin\\s*\\{([A-Za-z]+\\*?)\\}" 
		+ "|\\\\end\\s*\\{([A-Za-z]+\\*?)\\}"
		+ "|\\\\\\[|\\\\\\]"
		+ "|\\\\\\(|\\\\\\)"
		);
	
	int row, col, r, c;
	MathData begin, end;

	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	QString textline = getTextLineReal(doc,row);

	// check for '\ensuremath{...}'
	QString word;
	int x1, x2;
	if(getCurrentWord(doc, row, col, smTex, word, x1, x2) && word == "\\ensuremath") {
		view->setCursorPosition(KTextEditor::Cursor(row, x2));
	}

	BracketData open,close;
	if(getTexgroup(false, open, close, view)) {
		QString s = getTextLineReal(doc,open.row);
		if(open.col >= 11 && s.mid(open.col - 11, 11) == "\\ensuremath") {
			view->setCursorPosition(KTextEditor::Cursor(row, col));
			row1 = open.row;
			col1 = open.col-11;
			row2 = close.row;
			col2 = close.col;
			return true;
		}
	}

	// do we need to restore the cursor position
	view->setCursorPosition(KTextEditor::Cursor(row, col));

	// '$' is difficult, because it is used as opening and closing tag
	int mode = 0;
	if(textline[col] == '$') {
		mode = 1;
	}
	else if(col > 0 && textline[col - 1] == '$') {
		mode = 2;
	}

	if(mode > 0) {
		// first look, if this is a closing '$'
		r = row;
		c = (mode == 1) ? col : col - 1;
		if(decreaseCursorPosition(doc, r, c) && findOpenMathTag(doc, r, c, reg, begin)) {
			if(begin.tag == mmMathDollar && (begin.numdollar & 1)) {
				row1 = begin.row;
				col1 = begin.col;
				row2 = row;
				col2 = (mode == 1) ? col + 1 : col;
				return true;
			}
		}

		// perhaps an opening '$'
		r = row;
		c = (mode == 1) ? col+1 : col;
		if(findCloseMathTag(doc, r, c, reg, end)) {
			if(end.tag == mmMathDollar) {
				row1 = row;
				col1 = ( mode == 1 ) ? col : col-1;
				row2 = end.row;
				col2 = end.col + end.len;
				return true;
			}
		}

		// found no mathgroup with '$'
		return false;
	}

	// now let's search for all other math tags:
	// if a mathgroup tag starts in the current column, we save this 
	// position and move the cursor one column to the right
	bool openingtag = isOpeningMathTagPosition(doc, row, col, begin);
	if(openingtag) {
		// try to find the corresponding closing tag at the right
		bool closetag = findCloseMathTag(doc, row, col + 1, reg, end);
		if(closetag && checkMathtags(begin, end)) {
			row1 = begin.row;
			col1 = begin.col;
			row2 = end.row;
			col2 = end.col + end.len;
			return true;
		}
	}

	r = row;
	c = col;
	bool closingtag = isClosingMathTagPosition(doc, row, col, end);
	if(closingtag) {
		c = end.col;
		if(!decreaseCursorPosition(doc, r, c)) {
			return false;
		}
	}
		
	// now try to search to opening tag of the math group
	if(!findOpenMathTag(doc, r, c, reg, begin)) {
		return false;
	}

	if(begin.tag == mmMathDollar && !(begin.numdollar & 1)) {
		//KILE_DEBUG() << "error: even number of '$' --> no math mode" ;
		return false;
	}

	// and now the closing tag
	if(!findCloseMathTag(doc, r, c, reg, end)) {
		return false;
	}

	// both tags were found, but they must be of the same type
	if(checkMathtags(begin, end)) {
		row1 = begin.row;
		col1 = begin.col;
		row2 = end.row;
		col2 = end.col + end.len;
		return true;
	}
	else {
		return false;
	}
}

//////////////////// mathgroup tags ////////////////////

bool EditorExtension::checkMathtags(const MathData &begin,const MathData &end)
{
	// both tags were found, but they must be of the same type
	if(begin.tag != end.tag) {
		//KILE_DEBUG() << "error: opening and closing tag of mathmode don't match" ;
		return false;
	}

	// and additionally: if it is a math env, both tags must have the same name
	if(begin.tag == mmDisplaymathEnv && begin.envname != end.envname) {
		//KILE_DEBUG() << "error: opening and closing env tags have different names" ;
		return false;
	}

	return true;
}

bool EditorExtension::isOpeningMathTagPosition(KTextEditor::Document *doc, uint row, uint col, MathData &mathdata)
{
	QString textline = getTextLineReal(doc,row);

	QRegExp reg("\\\\begin\\s*\\{([A-Za-z]+\\*?)\\}|\\\\\\[|\\\\\\(");
	if((int)col != reg.search(textline, col)) {
		return false;
	}

	QChar id = reg.cap(0)[1];
	QString envname = reg.cap(1);

	mathdata.row = row;
	mathdata.col = col;
	mathdata.len = reg.cap(0).length();

	switch(id.ascii()) {
		case 'b':
			if(!(m_latexCommands->isMathEnv(envname) || envname=="math") || m_latexCommands->needsMathMode(envname)) {
				return false;
			}
			mathdata.tag = ( envname=="math" ) ? mmMathEnv : mmDisplaymathEnv;
			mathdata.envname = envname;
		break;
		case '[':
			mathdata.tag = mmDisplaymathParen;
		break;
		case '(':
			mathdata.tag = mmMathParen;
		break;
	}

	return true;
}

bool EditorExtension::isClosingMathTagPosition(KTextEditor::Document *doc, uint row, uint col,MathData &mathdata)
{
	QString textline = doc->line(row);

	QRegExp reg("\\\\end\\s*\\{([A-Za-z]+\\*?)\\}|\\\\\\]|\\\\\\)");
	int pos = reg.searchRev(textline,col);
	if(pos < 0 || (int)col > pos + reg.matchedLength()) {
		return false;
	}

	QChar id = reg.cap(0)[1];
	QString envname = reg.cap(1);

	mathdata.row = row;
	mathdata.col = pos;
	mathdata.len = reg.cap(0).length();

	switch(id.ascii()) {
		case 'e':
			if(!(m_latexCommands->isMathEnv(envname) || envname=="math") || m_latexCommands->needsMathMode(envname)) {
				return false;
			}
			mathdata.tag = ( envname=="math" ) ? mmMathEnv : mmDisplaymathEnv;
			mathdata.envname = envname;
		break;
		case ']':
			mathdata.tag = mmDisplaymathParen;
		break;
		case ')':
			mathdata.tag = mmMathParen;
		break;
	}

	return true;
}

bool EditorExtension::findOpenMathTag(KTextEditor::Document *doc, int row, int col, QRegExp &reg, MathData &mathdata)
{
	int lastrow = -1, lastcol = -1;
	QString mathname;

	bool foundDollar = false;
	uint numDollar = 0;
	
	KTextEditor::SearchInterface *iface;
	iface = dynamic_cast<KTextEditor::SearchInterface*>(doc);

	QString textline = getTextLineReal(doc, row);
	int column = col;

	bool continueSearch = true;
	while(continueSearch) {
		while((column = reg.searchRev(textline,col)) != -1) {
			col = column;

			mathdata.row = row;
			mathdata.col = col;
			mathdata.len = reg.cap(0).length();
			mathname = reg.cap(0).left(2);
		
			// should be better called 'isValidChar()', because it checks for comments
			// and escaped chars like backslash and dollar in '\\' and '\$'
			if(mathname == "$") {
				// count and continue search
				++numDollar;

				// but remember the first dollar found backwards 
				if(!foundDollar) {
					lastrow = row;
					lastcol = col;
					foundDollar = true;
				}
			}
			else if(mathname=="\\[" || mathname=="\\(") {
				// found start of mathmode
				if(numDollar == 0) {
					mathdata.tag = ( mathname == "\\[" ) ? mmDisplaymathParen : mmMathParen;
					mathdata.numdollar = 0;
					return true;
				}
				else {
					//KILE_DEBUG() << "error: dollar not allowed in \\[ or \\( mode";
					return false;
				}
			}
			else if(mathname=="\\]" || mathname=="\\)") {
				continueSearch = false;
				break;
			}
			else  if(mathname=="\\b") {
				// save name of environment
				QString envname = reg.cap(1);

				// if we found the opening tag of a math env
				if(m_latexCommands->isMathEnv(envname) || envname=="math") {
					if(numDollar > 0) {
						//KILE_DEBUG() << "error: dollar not allowed in math env   numdollar=" << numDollar;
						return false;
					}

					// if this is a math env with its own mathmode, we have found the starting position
					if(envname == "math") {
						mathdata.tag = mmMathEnv;
						mathdata.envname = envname;
						return true;
					}

					if(!m_latexCommands->needsMathMode(envname)) {
						mathdata.tag = mmDisplaymathEnv;
						mathdata.envname = envname;
						return true;
					}
				}
				// no math env, we found the opening tag of a normal env
				else {
					continueSearch = false;
					break;
				}
			}
			else if(mathname == "\\e") {
				QString envname = reg.cap(2);

				// if we found the closing tag of a math env
				if(m_latexCommands->isMathEnv(envname) || envname == "math") {
					// if this is a math env with its own mathmode
					if(!m_latexCommands->needsMathMode(envname) || envname == "math") {
						continueSearch = false;
						break;
					}

					// if this is a math env which needs $..$
					if(m_latexCommands->isMathModeEnv(envname)) {
						if(numDollar >= 1) {
							--numDollar;
							continueSearch = false;
							break;
						}
						// else continue search
					}
				}
				// if we found the closing tag of a normal env
				else {
					continueSearch = false;
					break;
				}
			}
			else {
				//KILE_DEBUG() << "error: unknown match";
				return false;
			}

			// continue search one column left of the last match (if this is possible)
			if(col == 0) {
				break;
			}

			--col;
		}

		if(row > 0) {
	 		textline = getTextLineReal(doc,--row);
			col = textline.length();
		}

		if(column == -1) {
			continueSearch = false;
			break;
		}
	}
	
	// nothing else found, so math mode starts a the last dollar (the first one found backwards)
	mathdata.row = lastrow;
	mathdata.col = lastcol;
	mathdata.len = 1;
	mathdata.numdollar = numDollar;
	
	mathdata.tag = (numDollar > 0) ? mmMathDollar : mmNoMathMode;
	
	return true;
}

#ifdef __GNUC__
#warning Redesign the findCloseMathTag method (line 835)!
#endif
//FIXME: fix for KDE4
bool EditorExtension::findCloseMathTag(KTextEditor::Document *doc, int row, int col, QRegExp &reg, MathData &mathdata)
{
/*
	KTextEditor::SearchInterface *iface;
	iface = dynamic_cast<KTextEditor::SearchInterface *>(doc);	

	uint rowFound,colFound,lenFound;
	while(iface->searchText(row, col, reg, &rowFound, &colFound, &lenFound, false)) {
		// should be better called 'isValidChar()', because it checks for comments
		// and escaped chars like backslash and dollar in '\\' and '\$'
		if(isValidBackslash(doc, rowFound, colFound)) {
			QString mathname = reg.cap(0).left(2);

			// always remember behind the last match
			mathdata.row = rowFound;
			mathdata.col = colFound;
			mathdata.len = lenFound;
		
			if(mathname=="$") {
				mathdata.tag = mmMathDollar;
				return true;
			}
			else if(mathname=="\\]") {
				mathdata.tag = mmDisplaymathParen;
				return true;
			}
			else if(mathname=="\\)") {
				mathdata.tag = mmMathParen;
				return true;
			}
			else if(mathname=="\\[" || mathname=="\\(") {
				//KILE_DEBUG() << "error: current mathgroup was not closed";
				return false;
			}
			else if(mathname=="\\b") {
				QString envname = reg.cap(1);
				
				if(!(m_latexCommands->isMathEnv(envname) || envname=="math")) {
					//KILE_DEBUG() << "error: only math env are allowed in mathmode (found begin tag)";
					return false;
				}

				if(!m_latexCommands->needsMathMode(envname) || envname=="math") {
					//KILE_DEBUG() << "error: mathenv with its own mathmode are not allowed in mathmode ";
					return false;
				}
				// else continue search
			}
			else if(mathname == "\\e") {
				QString envname = reg.cap(2);
				if(!(m_latexCommands->isMathEnv(envname) || envname=="math")) {
					//KILE_DEBUG() << "error: only math env are allowed in mathmode (found end tag)";
					return false;
				}

				if(envname == "math") {
					mathdata.tag = mmMathEnv;
					mathdata.envname = envname;
					return true;
				}

				if(!m_latexCommands->needsMathMode(envname)) {
					mathdata.tag = mmDisplaymathEnv;
					mathdata.envname = envname;
					return true;
				}

				// else continue search
			}
		}

		// continue search one column right of the last match (if this is possible)
		row = rowFound;
		col = colFound;
		if(!increaseCursorPosition(doc,row,col)) {
			break;
		}
	}
*/
	return false;
}

//////////////////// insert newlines inside an environment ////////////////////

// intelligent newlines: look for the last opened environment
// and decide what to insert
// or continue the comment

void EditorExtension::insertIntelligentNewline(KTextEditor::View *view)
{
	KILE_DEBUG() << "void EditorExtension::insertIntelligentNewline(KTextEditor::View *view)";
	
	view = determineView(view);
	
	if(!view) {
		return;
	}

	KTextEditor::Document* doc = view->document();
	
	if(!doc) {
		return;
	}

	int row, col;
	QString name;
	
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

		
	if(isCommentPosition(doc, row, col)) {
		KILE_DEBUG() << "found comment";
#ifdef __GNUC__
#warning The 'keyReturn' feature still needs to be ported somehow!
#endif
//FIXME: port for KDE4
// 		view->keyReturn();
		view->insertText("% ");
		return;
	}
	else if(findOpenedEnvironment(row, col, name, view)) {
		if(m_latexCommands->isListEnv(name)) {
#ifdef __GNUC__
#warning The 'keyReturn' feature still needs to be ported somehow!
#endif
//FIXME: port for KDE4
// 			view->keyReturn();
			view->insertText("\\item " );
			return;
		}
		else if(m_latexCommands->isTabularEnv(name) || m_latexCommands->isMathEnv(name)) {
			view->insertText(" \\\\");
		}
	}
	// - no comment position
	// - found no opened environment
	// - unknown environment
	// - finish tabular or math environment
#ifdef __GNUC__
#warning The 'keyReturn' feature still needs to be ported somehow!
#endif
//FIXME: port for KDE4
// 	view->keyReturn();
}

bool EditorExtension::findOpenedEnvironment(int &row, int &col, QString &envname, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return false;
	}

	// get current cursor position
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	EnvData env;
	int startrow = row;
	int startcol = col;
	
	//KILE_DEBUG() << "   close - start ";
	// accept a starting place outside an environment
	bool env_position = isEnvironmentPosition(doc, row, col, env);

	// We can also accept a column, if we are on the left side of an environment.
	// But we should decrease the current cursor position for the search.
	if(env_position && env.cpos != EnvInside) {
		if(env.cpos == EnvLeft && !decreaseCursorPosition(doc, startrow, startcol)) {
			return false;
		}
		env_position = false;
	}
	
	if(!env_position && findEnvironmentTag(doc, startrow, startcol, env, true)) {
		//KILE_DEBUG() << "   close - found begin env at:  " << env.row << "/" << env.col << " " << env.name;
		row = env.row;
		col = env.col;
		envname = env.name;
		return true;
	}
	else {
		return false;
	}
}

QStringList EditorExtension::findOpenedEnvironmentList(KTextEditor::View *view, bool position)
{
	QStringList envlist;

	view = determineView(view);
	if(view) {
		int currentRow, currentCol;
		KTextEditor::Document *doc = view->document();
		KTextEditor::Cursor cursor = view->cursorPosition();
		currentRow = cursor.line();
		currentCol = cursor.column();


		int row = currentRow;
		int col = currentCol;
		EnvData env;

		// check the starting position
		bool env_position = isEnvironmentPosition(doc, row, col, env);
		if(env_position) {
			// we are inside an environment tag: bad to complete
			if(env.cpos == EnvInside) {
				return envlist;
			}
			// we are left of an environment tag: go one position to the left
			if(env.cpos == EnvLeft) {
				if (!decreaseCursorPosition(doc, row, col)) {
					return envlist;
				}
			}
		}
	
		while (findEnvironmentTag(doc, row, col, env, true)) {
			row = env.row;
			col = env.col;

			if(position) {
				envlist << env.name + QString(",%1,%2").arg(row).arg(col);
			}
			else {
				envlist << env.name;
			}

			if(col == 0) {
				if (!decreaseCursorPosition(doc, row, col)) {
					break;
				}
			}
			view->setCursorPosition(KTextEditor::Cursor(row, col));
		}

		// reset cursor original position
		view->setCursorPosition(KTextEditor::Cursor(currentRow, currentCol));
	}
	
	return envlist;
}

//////////////////// select an environment  ////////////////////

void EditorExtension::selectEnvironment(bool inside, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	EnvData envbegin,envend;
	
	if (!view->selection() || !expandSelectionEnvironment(inside,view)) {
		if ( getEnvironment(inside,envbegin,envend,view) )
			view->setSelection(KTextEditor::Range(envbegin.row, envbegin.col, envend.row, envend.col));
	}
}

void EditorExtension::deleteEnvironment(bool inside, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	EnvData envbegin,envend;
	
	if(getEnvironment(inside, envbegin, envend, view)) {
		KTextEditor::Document *doc = view->document();
		view->removeSelection();
		doc->removeText(KTextEditor::Range(envbegin.row, envbegin.col, envend.row, envend.col));
		view->setCursorPosition(KTextEditor::Cursor(envbegin.row, 0));
	}
}

// calculate start and end of an environment

bool EditorExtension::getEnvironment(bool inside, EnvData &envbegin, EnvData &envend, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return false;
	}

	int row, col;
	
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	if(!findBeginEnvironment(doc, row, col, envbegin)) {
		return false;
	}
	if(!findEndEnvironment(doc, row, col, envend)) {
		return false;
	}
	
	if(inside) {
		// check first line
		envbegin.col += envbegin.len;
		if (envbegin.col >= doc->lineLength(envbegin.row)) {
			++envbegin.row;
			envbegin.col = 0;
		}
	}
	else {
		envend.col += envend.len;
		// check last line
		if(envbegin.col == 0 && envend.col == doc->lineLength(envend.row)) {
			++envend.row;
			envend.col = 0;
		}
	}
	
	return true;
}

// determine text, startrow and startcol of current environment

QString EditorExtension::getEnvironmentText(int &row, int &col, QString &name, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return QString();
	}

	EnvData envbegin, envend;
	
	if(getEnvironment(false, envbegin, envend, view) && envbegin.name != "document") {
		row = envbegin.row;
		col = envbegin.col;
		name = envbegin.name;
		return view->document()->text(KTextEditor::Range(envbegin.row, envbegin.col, envend.row, envend.col));
	}
	else {
		return QString();
	}
}

bool EditorExtension::hasEnvironment(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return false;
	}

	EnvData envbegin,envend;
	return (getEnvironment(false, envbegin, envend, view) && envbegin.name != "document");
}

// when an environment is selected (inside or outside), 
// the selection is expanded to the surrounding environment

bool EditorExtension::expandSelectionEnvironment(bool inside, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return false;
	}

	KTextEditor::Document *doc = view->document();
	if (!view->selection()) {
		return false;
	}

	// get current position
	int row, col;
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	
	// get current selection
	KTextEditor::Range selectionRange = view->selectionRange();
	int row1 = selectionRange.start().line();
	int col1 = selectionRange.start().column();
	int row2 = selectionRange.end().line();
	int col2 = selectionRange.start().column();

	// determine current environment outside 
	EnvData oenvbegin,oenvend;
	if(!getEnvironment(false, oenvbegin, oenvend, view)) {
		return false;
	}
	
	bool newselection = false;
	// first look, if this environment is selected outside
	if(row1 == oenvbegin.row && col1 == oenvbegin.col && row2 == oenvend.row && col2 == oenvend.col) {
		if(!decreaseCursorPosition(doc, oenvbegin.row, oenvbegin.col) ) {
			return newselection;
		}
		view->setCursorPosition(KTextEditor::Cursor(oenvbegin.row, oenvbegin.col));
		// search the surrounding environment and select it
		if(getEnvironment(inside, oenvbegin, oenvend, view)) {
			view->setSelection(KTextEditor::Range(oenvbegin.row, oenvbegin.col, oenvend.row, oenvend.col));
			newselection = true;
		}
	}
	else {
		// then determine current environment inside 
		EnvData ienvbegin, ienvend;
		getEnvironment(true, ienvbegin, ienvend, view);
		// and look, if this environment is selected inside
		if(row1 == ienvbegin.row && col1 == ienvbegin.col && row2 == ienvend.row && col2 == ienvend.col) {
			if(!decreaseCursorPosition(doc, oenvbegin.row, oenvbegin.col) ) {
				return newselection;
			}
			view->setCursorPosition(KTextEditor::Cursor(oenvbegin.row, oenvbegin.col));
			// search the surrounding environment and select it
			if(getEnvironment(inside, ienvbegin, ienvend, view)) {
				view->setSelection(KTextEditor::Range(ienvbegin.row, ienvbegin.col, ienvend.row, ienvend.col));
				newselection = true;
			}
		}
	} 
	
	// restore old cursor position
	view->setCursorPosition(KTextEditor::Cursor(row, col));
	return newselection;
}

//////////////////// search for \begin{env}  ////////////////////

// Find the last \begin{env} tag. If the current cursor is over
//  - \begin{env} tag: we will stop immediately
//  - \end{env} tag: we will start before this tag

bool EditorExtension::findBeginEnvironment(KTextEditor::Document *doc, int row, int col, EnvData &env)
{
	// KILE_DEBUG() << "   find begin:  ";
	if(isEnvironmentPosition(doc, row, col, env)) {
		// already found position?
		//KILE_DEBUG() << "   found env at:  " << env.row << "/" << env.col << " " << env.name;
		if(env.tag == EnvBegin) {
		//KILE_DEBUG() << "   is begin env at:  " << env.row << "/" << env.col << " " << env.name;
			return true;
		}
	
		// go one position back
		//KILE_DEBUG() << "   is end env at:  " << env.row << "/" << env.col << " " << env.name;
		row = env.row;
		col = env.col;
		if(!decreaseCursorPosition(doc, row, col)) {
			return false;
		}
	}
	
	// looking back for last environment
	//KILE_DEBUG() << "   looking back from pos:  " << row << "/" << col << " " << env.name;
	return findEnvironmentTag(doc, row, col, env, true);
}

//////////////////// search for \end{env}  ////////////////////

// Find the last \end{env} tag. If the current cursor is over
//  - \end{env} tag: we will stop immediately
//  - \begin{env} tag: we will start behind this tag

bool EditorExtension::findEndEnvironment(KTextEditor::Document *doc, int row, int col, EnvData &env)
{
	if(isEnvironmentPosition(doc, row, col, env)) {
		// already found position?
		if(env.tag == EnvEnd ) {
			return true;
		}

		// go one position forward
		row = env.row;
		col = env.col + 1;
	}

	// looking forward for the next environment
	return findEnvironmentTag(doc, row, col, env, false);
}

//////////////////// search for an environment tag ////////////////////

// find the last/next non-nested environment tag

#ifdef __GNUC__
#warning Redesign the findEnvironmentTag method (line 1327)!
#endif
//FIXME: fix for KDE4
bool EditorExtension::findEnvironmentTag(KTextEditor::Document *doc, int row, int col, EnvData &env,bool backwards)
{
/*
	KTextEditor::SearchInterface *iface;
	iface = dynamic_cast<KTextEditor::SearchInterface *>(doc);
	
	uint envcount = 0;
	
	EnvTag wrong_env = ( backwards ) ? EnvEnd : EnvBegin;
	while ( iface->searchText(row,col,m_reg,&env.row,&env.col,&env.len,backwards) )
	{
		if ( isValidBackslash(doc,env.row,env.col) )
		{
			EnvTag found_env = ( m_reg.cap(2)=="begin" || m_reg.cap(4)=="\\[" ) ? EnvBegin : EnvEnd; 
			if ( found_env == wrong_env )
			{
				++envcount;
			}
			else
			{
				if ( envcount > 0 )
					--envcount;
				else
				{
					if ( found_env == EnvBegin ) 
					{
						env.name = ( m_reg.cap(2)=="begin" ) ? m_reg.cap(3) : "\\[";
					}
					else
					{
						env.name = ( m_reg.cap(2)=="end" ) ? m_reg.cap(3) : "\\]";
					}	
					env.tag = found_env;
					return true;
				}
			}
		}
	
		// new start position
		if ( !backwards )
		{
			row = env.row;
			col = env.col + 1;
		}
		else
		{
			row = env.row;
			col = env.col;
			if ( ! decreaseCursorPosition(doc,row,col) )
				return false;
		}
	}
*/
	return false;
}

//////////////////// check for an environment position ////////////////////

// Check if the current position belongs to an environment. The result is set
// to the beginning backslash of the environment tag. The same algorithms as
// matching brackets is used.

bool EditorExtension::isEnvironmentPosition(KTextEditor::Document *doc, int row, int col, EnvData &env)
{
	// get real textline without comments, quoted characters and pairs of backslashes
	QString textline = getTextLineReal(doc, row);
	
	if(col > textline.length()) {
		return false;
	}
	
	EnvData envright;
	bool left = false;
	bool right = false;
	
	//KTextEditor::SearchInterface *iface;
	//iface = dynamic_cast<KTextEditor::SearchInterface *>(doc);
	
	// check if there is a match in this line from the current position to the left
	int startcol = (textline[col] == '\\') ? col - 1 : col;
	if(startcol >= 1) {
		int pos = textline.findRev(m_reg,startcol);
		env.len = m_reg.matchedLength();
		if(pos != -1 && pos < col && col <= pos + env.len) {
			env.row = row;
			env.col = pos;
			QChar ch = textline.at(pos + 1);
			if(ch=='b' || ch=='e') {
				env.tag = (ch == 'b') ? EnvBegin : EnvEnd;
				env.name = m_reg.cap(3);
			}
			else {
				env.tag = (ch == '[') ? EnvBegin : EnvEnd;
				env.name = m_reg.cap(4);
			}
			env.cpos = (col < pos + env.len) ? EnvInside : EnvRight;
			// we have already found a tag, if the cursor is inside, but not behind this tag
			if(env.cpos == EnvInside) {
				return true;
			}
			left = true;
		//KILE_DEBUG() << "   is - found left:  pos=" << pos << " " << env.name << " " << QString(textline.at(pos+1));
		}
	}
	
	// check if there is a match in this line from the current position to the right
	if (textline[col] == '\\' && col == textline.indexOf(m_reg, col)) {
		envright.row = row;
		envright.col = col;
		envright.len = m_reg.matchedLength();
		QChar ch = textline.at(col+1);
		if(ch == 'b' || ch == 'e') { // found "\begin" or "\end"
			envright.tag = ( ch == 'b' ) ? EnvBegin : EnvEnd;
			envright.name = m_reg.cap(3);
		}
		else { // found "\[" or "\\]"
			envright.tag = (ch == '[') ? EnvBegin : EnvEnd;
			envright.name = m_reg.cap(4);
		}
		envright.cpos = EnvLeft;
		right = true;
		//KILE_DEBUG() << "   is - found right:  pos=" <<col << " " << envright.name << " " << QString(textline.at(col+1));
	}
	
	//KILE_DEBUG() << "found left/right: " << left << "/" << right;
	// did we find a tag?
	if(!(left || right)) {
		return false;
	}
	
	// now check, which tag we should be taken (algorithm like matching brackets)
	
	if(m_overwritemode) {
		if(right && envright.tag == EnvBegin) {
			env = envright;
			return true;
		}
		else if(left && env.tag == EnvEnd) {
			return true;
		}
		else {
			return false;
		}
	}
	else if(left && env.tag == EnvEnd) {
		//KILE_DEBUG() << "   1: accept left end:  " << env.name;
		return true;
	}
	else if(right && envright.tag == EnvBegin) {
		//KILE_DEBUG() << "   2: accept right begin:  " << envright.name;
		env = envright;
	}
	else if(left && env.tag == EnvBegin) {
		// KILE_DEBUG() << "   3: accept left begin:  " << env.name;
		return true;
	}
	else if(right && envright.tag == EnvEnd) {
		//KILE_DEBUG() << "   4: accept right end:  " << envright.name;
		env = envright;
	}
	else {
		return false;
	}

	return true;
}

//////////////////// check for a comment ////////////////////

// check if the current position is within a comment

bool EditorExtension::isCommentPosition(KTextEditor::Document *doc, int row, int col)
{
	QString textline = doc->line(row);
	
	bool backslash = false;
	for(int i = 0; i < col; ++i) {
		if(textline[i] == '%') {
			if(!backslash) { // found a comment sign
				return true;
			}
			else {
				backslash = false;
			}
		}
		else if(textline[i] == '\\') { // count number of backslashes
			backslash = !backslash;
		}
		else {
			backslash = false;               // no backslash
		}
	}
	
	return false;
}

// check if the character at text[col] is a valid backslash:
//  - there is no comment sign in this line before
//  - there is not a odd number of backslashes directly before

bool EditorExtension::isValidBackslash(KTextEditor::Document *doc, int row, int col)
{
	QString textline = doc->line(row);
	
	bool backslash = false;
	for(int i = 0; i < col; ++i) {
		if(textline[i] == '%') {
			if(!backslash) {
				return false;                 // found a comment sign
			}
			else {
				backslash = false;
			}
		}
		else if(textline[i] == '\\') {  // count number of backslashes
			backslash = !backslash;
		}
		else {
			backslash = false;               // no backslash
		}
	}
	
	return !backslash;
}

//////////////////// goto next bullet ////////////////////

void EditorExtension::gotoBullet(bool backwards, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int row, col, ypos, xpos;
	unsigned int len;

	// get current position
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	
	// change the start position or we will stay at this place
	if(backwards) {
		if(!decreaseCursorPosition(doc, row, col)) {
			return;
		}
	}
	else {
		if(!increaseCursorPosition(doc, row, col)) {
			return;
		}
	}

#ifdef __GNUC__
#warning Fix usage of KTextEditor::SearchInterface (line 1588)!
#endif
//FIXME: fix for KDE4
/*
	if(doc->searchText(row, col, s_bullet, &ypos, &xpos, &len, true, backwards)) {
		view->setSelection(KTextEditor::Range(ypos, xpos, ypos, xpos + 1));
		view->setCursorPosition(KTextEditor::Cursor(ypos, xpos));
	}
*/
}

//////////////////// increase/decrease cursor position ////////////////////

bool EditorExtension::increaseCursorPosition(KTextEditor::Document *doc, int &row, int &col)
{
	bool ok = true;
	
	if(col < doc->lineLength(row) - 1) {
		++col;
	}
	else if(row < doc->lines() - 1) {
		++row;
		col = 0;
	}
	else {
		ok = false;
	}

	return ok;
}

bool EditorExtension::decreaseCursorPosition(KTextEditor::Document *doc, int &row, int &col)
{
	bool ok = true;
	
	if(col > 0) {
		--col;
	}
	else if(row > 0) {
		--row;
		col = doc->lineLength(row);
	}
	else {
		ok = false;
	}

	return ok;
}

//////////////////// texgroups ////////////////////

// goto the next non-nested bracket

void EditorExtension::gotoTexgroup(bool backwards, KTextEditor::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	uint row,col;
	bool found;
	BracketData bracket;
	
	// get current position
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	m_overwritemode = (view->viewEditMode() == KTextEditor::View::EditOverwrite);
	
	// start searching
	if ( backwards )
		found = findOpenBracket(doc,row,col,bracket);
	else
	{
		found = findCloseBracket(doc,row,col,bracket);
		// go behind the bracket
		if ( ! m_overwritemode )
		++bracket.col;
	}
	
	if ( found )
		view->setCursorPosition(KTextEditor::Cursor(bracket.row, bracket.col));
}

// match the opposite bracket

void EditorExtension::matchTexgroup(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int row, col;
	BracketData bracket;
	
	// get current position
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	m_overwritemode = (view->viewEditMode() == KTextEditor::View::EditOverwrite);
	
	// this operation is only allowed at a bracket position
	if(!isBracketPosition(doc, row, col, bracket)) {
		return;
	}
	
	// start searching
	bool found = false;
	if(bracket.open) {
		found = findCloseBracketTag(doc, bracket.row, bracket.col + 1, bracket);
		// go behind the bracket
		if(!m_overwritemode) {
			++bracket.col;
		}
	}
	else {
		if(!decreaseCursorPosition(doc, bracket.row, bracket.col)) {
			return;
		}
		found = findOpenBracketTag(doc, bracket.row, bracket.col, bracket);
	}
	
	if(found) {
		view->setCursorPosition(KTextEditor::Cursor(bracket.row, bracket.col));
	}
}

//////////////////// close an open texgroup  ////////////////////

// search for the last opened texgroup and close it

void EditorExtension::closeTexgroup(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int row, col;
	BracketData bracket;
	
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	
	int rowtemp = row;
	int coltemp = col;
	if(!decreaseCursorPosition(doc, rowtemp, coltemp)) {
		return;
	}
	
	if(findOpenBracketTag(doc, rowtemp, coltemp, bracket)) {
		doc->insertText(KTextEditor::Cursor(row, col), "}");
		view->setCursorPosition(KTextEditor::Cursor(row, col + 1));
	}
}

//////////////////// select a texgroup  ////////////////////

void EditorExtension::selectTexgroup(bool inside, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	BracketData open,close;
	
	if(getTexgroup(inside, open, close, view)) {
		KTextEditor::Document *doc = view->document();
		view->setSelection(KTextEditor::Range(open.row, open.col, close.row, close.col));
	}
}

void EditorExtension::deleteTexgroup(bool inside, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	BracketData open, close;

	if(getTexgroup(inside, open, close, view)) {
		KTextEditor::Document *doc = view->document();
		view->removeSelection();
		doc->removeText(KTextEditor::Range(open.row, open.col, close.row, close.col));
		view->setCursorPosition(KTextEditor::Cursor(open.row, open.col + 1));
	}
}

// calculate start and end of an environment

bool EditorExtension::getTexgroup(bool inside, BracketData &open, BracketData &close, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return false;
	}

	int row, col;
	
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	
	if(!findOpenBracket(doc, row, col, open))  {
		//KILE_DEBUG() << "no open bracket"; 
		return false;
	}
	if(!findCloseBracket(doc, row, col, close)) {
		//KILE_DEBUG() << "no close bracket"; 
		return false;
	}
	
	if(inside) {
		++open.col;
	}
	else {
		++close.col;
	}

	return true;
}

//////////////////// search for a bracket position  ////////////////////

// Find the last opening bracket. If the current cursor is over
//  - '{': we will stop immediately
//  - '}': we will start before this character

bool EditorExtension::findOpenBracket(KTextEditor::Document *doc, int row, int col, BracketData &bracket)
{
	if(isBracketPosition(doc, row, col, bracket)) {
		// already found position?
		if(bracket.open) {
			return true;
		}
		
		// go one position back
		row = bracket.row;
		col = bracket.col;
		if(!decreaseCursorPosition(doc, row, col)) {
			return false;
		}
	}
	
	// looking back for last bracket
	return findOpenBracketTag(doc, row, col, bracket);
}

// Find the last closing bracket. If the current cursor is over
//  - '}': we will stop immediately
//  - '{': we will start behind this character

bool EditorExtension::findCloseBracket(KTextEditor::Document *doc, int row, int col, BracketData &bracket)
{
	if (isBracketPosition(doc, row, col, bracket)) {
		// already found position?
		if(!bracket.open) {
			return true;
		}

		// go one position forward
		row = bracket.row;
		col = bracket.col + 1;
	}
	
	// looking forward for next bracket
	return findCloseBracketTag(doc, row, col, bracket);
}

/*
   Bracket matching uses the following algorithm (taken from Kate):
   1) If in overwrite mode, match the bracket currently underneath the cursor.
   2) Otherwise, if the character to the left of the cursor is an ending bracket,
      match it.
   3) Otherwise if the character to the right of the cursor is a
      starting bracket, match it.
   4) Otherwise, if the the character to the left of the cursor is an
      starting bracket, match it.
   5) Otherwise, if the character to the right of the cursor is an
      ending bracket, match it.
   6) Otherwise, don't match anything.
*/

bool EditorExtension::isBracketPosition(KTextEditor::Document *doc, int row, int col, BracketData &bracket)
{
	// default results
	bracket.row = row;
	bracket.col = col;
	
	QString textline = getTextLineReal(doc, row);
	QChar right = textline[col];
	QChar left  = (col > 0) ? textline[col-1] : QChar(' ');
	
	if (m_overwritemode) {
		if(right == '{') {
			bracket.open = true;
		}
		else if(left == '}') {
			bracket.open = false;
		}
		else {
			return false;
		}
	}
	else if(left == '}') {
		bracket.open = false;
		--bracket.col;
	}
	else if(right == '{') {
		bracket.open = true;
	}
	else if(left == '{') {
		bracket.open = true;
		--bracket.col;
	}
	else if(right == '}') {
		bracket.open = false;
	}
	else {
		return false;
	}

	return true;
}

// find next non-nested closing bracket

bool EditorExtension::findCloseBracketTag(KTextEditor::Document *doc, int row, int col, BracketData &bracket)
{
	uint brackets = 0;
	for(int line = row; line < doc->lines(); ++line) {
		uint start = (line == row) ? col : 0;
		QString textline = getTextLineReal(doc,line);
		for(int i = start; i < textline.length(); ++i) {
			if(textline[i] == '{') {
				++brackets;
			}
			else if(textline[i] == '}') {
				if(brackets > 0) {
					--brackets;
				}
				else {
					bracket.row = line;
					bracket.col = i;
					bracket.open = false;
					return true;
				}
			}
		}
	}
	
	return false;
}

// find next non-nested opening bracket

bool EditorExtension::findOpenBracketTag(KTextEditor::Document *doc, int row, int col, BracketData &bracket)
{
	uint brackets = 0;
	for(int line = row; line >= 0; --line) {
		QString textline = getTextLineReal(doc, line);
		int start = (line == row) ? col : textline.length() - 1;
		for (int i = start; i >= 0; --i) {
			//KILE_DEBUG() << "findOpenBracketTag: (" << line << "," << i << ") = " << textline[i].toLatin1();
			if(textline[i] == '{') {
				if(brackets > 0) {
					--brackets;
				}
				else {
					bracket.row = line;
					bracket.col = i;
					bracket.open = true;
					return true;
				}
			}
			else if(textline[i] == '}') {
				++brackets;
			}
		}
	}
	
	//KILE_DEBUG() << "nothting found";
	return false;
}

//////////////////// get real text ////////////////////

// get current textline and remove
//  - all pairs of backslashes: '\\'
//  - all quoted comment signs: '\%'
//  - all quoted brackets: '\{' and '\}'
//  - all comments
// replace these characters one one, which never will be looked for

QString EditorExtension::getTextLineReal(KTextEditor::Document *doc, int row)
{
	QString textline = doc->line(row);
	int len = textline.length();
	if(len == 0) {
		return QString();
	}

	bool backslash = false;
	for(int i = 0; i < len; ++i) {
		if (textline[i]=='{' || textline[i]=='}' || textline[i]=='$') {
			if(backslash) {
				textline[i-1] = '&';
				textline[i] = '&';
			}
			backslash = false;
		}
		else if(textline[i] == '\\') {
			if(backslash) {
				textline[i-1] = '&';
				textline[i] = '&';
				backslash = false;
			}
			else {
				backslash = true;
			}
		}
		else if(textline[i]=='%') {
			if (backslash) {
				textline[i-1] = '&';
				textline[i] = '&';
			}
			else {
				len = i;
				break;
			}
			backslash = false;
		}
		else {
			backslash = false;
		}
	}
	
	// return real text
	return textline.left(len);
}

//////////////////// capture the current word ////////////////////

// Capture the current word from the cursor position to the left and right.
// The result depens on the given search mode;
// - smTex       only letters, except backslash as first and star as last  character
// - smLetter:   only letters
// - smWord:     letters and digits
// - smNospace:  everything except white space

bool EditorExtension::getCurrentWord(KTextEditor::Document *doc, int row, int col, EditorExtension::SelectMode mode, QString &word, int &x1, int &x2)
{
	// get real textline without comments, quoted characters and pairs of backslashes
	QString textline = getTextLineReal(doc, row);
	if (col > textline.length()) {
		return false;
	}
	
	QRegExp reg;
	QString pattern1, pattern2;
	switch(mode) {
		case smLetter:
			pattern1 = "[^a-zA-Z]+";
			pattern2 = "[a-zA-Z]+";
		break;
		case smWord:
			pattern1 = "[^a-zA-Z0-9]";
			pattern2 = "[a-zA-Z0-9]+";
		break;
		case smNospace:
			pattern1 = "\\s";
			pattern2 = "\\S+";
		break;
		default:
			pattern1 = "[^a-zA-Z]";
			pattern2 = "\\\\?[a-zA-Z]+\\*?";
		break;
	}
	x1 = x2 = col;
	
	int pos;
	// search to the left side
	if(col > 0) {
		reg.setPattern(pattern1);
		pos = textline.findRev(reg,col-1);
		if(pos != -1) {        // found an illegal character
			x1 = pos + 1;
			if(mode == smTex) {
				if(textline[pos] == '\\') {
					x1 = pos;
				}
				col = x1;
			}
		}
		else {
			x1 = 0;               // pattern matches from beginning of line
		}
	}
	
	// search at the current position
	reg.setPattern(pattern2);
	pos = textline.indexOf(reg, col);
	if(pos != -1 && pos == col) {
		x2 = pos + reg.matchedLength();
	}
	
	// get all characters
	if(x1 != x2) {
		word = textline.mid(x1, x2 - x1);
		return true;
	}
	else {
		return false;
	}
}


//////////////////// paragraph ////////////////////

void EditorExtension::selectParagraph(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int startline, endline;
	
	if(findCurrentTexParagraph(startline, endline, view)) {
		view->setSelection(KTextEditor::Range(startline, 0, endline + 1, 0));
	}
}

void EditorExtension::deleteParagraph(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}
	int startline, endline;
	
	if(findCurrentTexParagraph(startline, endline, view)) {
		KTextEditor::Document *doc = view->document();
		view->removeSelection();
		if(startline > 0) {
			--startline;
		}
		else if(endline < doc->lines() - 1) {
			++endline;
		}
		doc->removeText(KTextEditor::Range(startline, 0, endline+1, 0));
		view->setCursorPosition(KTextEditor::Cursor(startline, 0));
	}
}

// get the range of the current paragraph

bool EditorExtension::findCurrentTexParagraph(int &startline, int &endline, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return false;
	}

	int row, col;
	
	// get current position
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	
	// don't accept an empty line as part of a paragraph
	if(doc->line(row).trimmed().isEmpty()) {
		return false;
	}
	
	// settings default results
	startline = row;
	endline = row;
	
	// find the previous empty line
	for(int line = row - 1; line >= 0; --line) {
		if(doc->line(line).trimmed().isEmpty()) {
			break;
		}
		startline = line;
	}
	
	// find the next empty line
	for(int line = row + 1; line < doc->lines(); ++line) {
		if(doc->line(line).trimmed().isEmpty()) {
			break;
		}
		endline = line;
	}
	
	// settings result
	return true;
}

void EditorExtension::gotoNextParagraph(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	bool found;
	int startline, endline;
	KTextEditor::Document *doc = view->document();

	endline = view->cursorPosition().line();
	if(doc->line(endline).trimmed().isEmpty()) {
		found = true;
	}
	else {
		found = findCurrentTexParagraph(startline, endline, view);
	}

	// we are in an empty line or in the last line of a paragraph
	if (found) {
		// find the next non empty line
		for(int line = endline + 1; line < doc->lines(); ++line) {
			if(!doc->line(line).trimmed().isEmpty()) {
				view->setCursorPosition(KTextEditor::Cursor(line, 0));
				return;
			}
		}
	}
}

void EditorExtension::gotoPrevParagraph(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	bool found;
	int startline,endline;
	KTextEditor::Document *doc = view->document();

	startline = view->cursorPosition().line();
	if(doc->line(startline).trimmed().isEmpty()) {
		startline++;
		found = true;
	}
	else {
		found = findCurrentTexParagraph(startline,endline,view);
	}
	// we are in an empty line or in the first line of a paragraph
	if(found) {
		// find the last line of the previous paragraph
		int foundline = -1;
		for (int line = startline - 1; line >= 0; --line) {
			if(!doc->line(line).trimmed().isEmpty()) {
				break;
			}
			foundline = line;
		}
		if(foundline < 0) {
			return;
		}

		// and finally the first line of this paragraph
		int prevstartline = -1;
		for(int line = foundline - 1; line >= 0; --line) {
			if(doc->line(line).trimmed().isEmpty()) {
				break;
			}
			prevstartline = line;
		}

		if(prevstartline >= 0) {
			view->setCursorPosition(KTextEditor::Cursor(prevstartline, 0));
		}
	}
}

//////////////////// gotoLine ////////////////////

void EditorExtension::gotoLine(KTextEditor::View *view)
{
	view = determineView(view);
	if (view) {
#ifdef __GNUC__
#warning The 'gotoLine' feature still needs to be ported somehow (line 2255)!
#endif
//FIXME: port for KDE4
// 		view->gotoLine();
	}
}

//////////////////// one line of text////////////////////

void EditorExtension::selectLine(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	// get current position
	int row, col;
	QString word;
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();
	
	if(doc->lineLength(row) > 0) {
		view->setSelection(KTextEditor::Range(row, 0, row + 1, 0));
	}
}

void EditorExtension::deleteEndOfLine(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int row, col;
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	KTextEditor::Document *doc = view->document();
	view->removeSelection();
	doc->removeText(KTextEditor::Range(row, col, row, doc->lineLength(row)));
}

//////////////////// LaTeX command ////////////////////

void EditorExtension::selectWord(EditorExtension::SelectMode mode, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	// get current position
	int row, col, col1, col2;
	QString word;
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	if(getCurrentWord(doc, row, col, mode, word, col1, col2)) {
		view->setSelection(KTextEditor::Range(row, col1, row, col2));
	}
}

void EditorExtension::deleteWord(EditorExtension::SelectMode mode, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	// get current position
	int row, col, col1, col2;
	QString word;
	KTextEditor::Document *doc = view->document();
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	if(getCurrentWord(doc, row, col, mode, word, col1, col2)) {
		doc->removeText(KTextEditor::Range(row, col1, row, col2));
	}
}

void EditorExtension::nextBullet(KTextEditor::View* view)
{
	gotoBullet(false, view);
}

void EditorExtension::prevBullet(KTextEditor::View* view)
{
	gotoBullet(true, view);
}

void EditorExtension::insertBullet(KTextEditor::View* view)
{
	int col, pos;
	view = determineView(view);
	if(!view) {
		return;
	}

	view->document()->insertText(view->cursorPosition(), s_bullet);
}

void EditorExtension::completeWord()
{
	complete()->editComplete(m_ki->viewManager()->currentTextView(), KileDocument::CodeCompletion::cmLatex);
}

void EditorExtension::completeEnvironment()
{
	complete()->editComplete(m_ki->viewManager()->currentTextView(), KileDocument::CodeCompletion::cmEnvironment);
}

void EditorExtension::completeAbbreviation()
{
	complete()->editComplete(m_ki->viewManager()->currentTextView(), KileDocument::CodeCompletion::cmAbbreviation);
}

//////////////////// double quotes ////////////////////

void EditorExtension::initDoubleQuotes()
{
	m_dblQuotes = KileConfig::insertDoubleQuotes();
	
	int index = KileConfig::doubleQuotes();
	if(index < 0 && index >= m_quoteList.count()) {
		index = 0;
	}
	
	QStringList quotes = m_quoteList[index].split(QRegExp("\\s{2,}"));
	m_leftDblQuote = quotes[1];
	m_rightDblQuote = quotes[2];
	KILE_DEBUG() << "new quotes: " << m_dblQuotes << " left=" << m_leftDblQuote << " right=" << m_rightDblQuote<< endl;
}

bool EditorExtension::insertDoubleQuotes()
{
	// don't insert double quotes, if konsole has focus 
	// return false, because if this is called from an event
	// handler, because this event has to be passed on
	if(m_ki->texKonsole()->hasFocus()) {
		return false;
	}

	// insert double quotes, normal mode or autocompletion mode
	// always return true for event handler
	KTextEditor::View *view = determineView(NULL);
	if(!view) {
		return true;
	}
	
	int row, col;
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	KTextEditor::Document *doc = view->document();

	if(doc && m_ki->extensions()->isTexFile(doc->url())) {
		view->removeSelectionText();
	}
	else {
		return false;
	}

	// simply insert, if we are inside a verb command
	if(insideVerb(view) || insideVerbatim(view)) {
		return false;
	}

	// simply insert, if autoinsert mode is not active or the char bevor is \ (typically for \"a useful)
	if (!m_dblQuotes || (col > 0 && doc->text(KTextEditor::Range(row, col - 1, row, col)) == "\\")) {
		return false;
	}

	// insert with auto mode
	KTextEditor::SearchInterface *iface;
	iface = dynamic_cast<KTextEditor::SearchInterface *>(doc);
		
	QString pattern1 = QRegExp::escape(m_leftDblQuote);
	if(m_leftDblQuote.at(m_leftDblQuote.length()-1).isLetter()) {
		pattern1 += "(\\b|(\\{\\}))";
	}
	QString pattern2 = QRegExp::escape(m_rightDblQuote);
	if(m_rightDblQuote.at(m_rightDblQuote.length()-1).isLetter()) {
		pattern2 += "(\\b|(\\{\\}))";
	}

	QRegExp reg('(' + pattern1 + ")|(" + pattern2 + ')');

	int r, c, l;
	bool openfound = false;
#ifdef __GNUC__
#warning Fix usage of KTextEditor::SearchInterface (line 2453)!
#endif
//FIXME: fix for KDE4
// 	if ( iface->searchText(row,col,reg,&r,&c,&l,true) )  
// 	{
// 		openfound = ( doc->line(r).indexOf(m_leftDblQuote,c) == (int)c );
		//KILE_DEBUG() << "pattern=" << reg.pattern() << " " << reg.cap(1) << " r=" << r << " c=" << c << " open=" << openfound<< endl;
// 	}
	
	QString textline = doc->line(row);
	//KILE_DEBUG() << "text=" << textline << " open=" << openfound;
	if(openfound) {
		// If we last inserted a language specific doublequote open,  
		// we have to change it to a normal doublequote. If not we 
		// insert a language specific doublequote close
		int startcol = col - m_leftDblQuote.length();
		//KILE_DEBUG() << "startcol=" << startcol << " col=" << col ;
		if (startcol>=0 && textline.indexOf(m_leftDblQuote, startcol) == startcol) {
				doc->removeText(KTextEditor::Range(row, startcol, row, startcol + m_leftDblQuote.length()));
				doc->insertText(KTextEditor::Cursor(row, startcol), "\"");
		}
		else {
			doc->insertText(KTextEditor::Cursor(row, col), m_rightDblQuote);
		}
	}
	else {
		// If we last inserted a language specific doublequote close,  
		// we have to change it to a normal doublequote. If not we 
		// insert a language specific doublequote open
		int startcol = col - m_rightDblQuote.length();
		//KILE_DEBUG() << "startcol=" << startcol << " col=" << col ;
		if (startcol >= 0 && textline.indexOf(m_rightDblQuote, startcol) == startcol) {
			doc->removeText(KTextEditor::Range(row, startcol, row, startcol + m_rightDblQuote.length()));
			doc->insertText(KTextEditor::Cursor(row,startcol), "\"");
		}
		else {
			doc->insertText(KTextEditor::Cursor(row, col), m_leftDblQuote);
		}
	}
	return true;
}

//////////////////// insert tabulator ////////////////////
	
void EditorExtension::insertIntelligentTabulator()
{
	KTextEditor::View *view = determineView(NULL);
	if(!view) {
		return;
	}

	int row, col, currentRow, currentCol;
	QString envname,tab;
	QString prefix = " ";
	
	KTextEditor::Cursor cursor = view->cursorPosition();
	currentRow = cursor.line();
	currentCol = cursor.column();
	if(findOpenedEnvironment(row, col, envname, view)) {
		// look if this is an environment with tabs
		tab = m_latexCommands->getTabulator(envname);
		
		// try to align tabulator with textline above
		if(currentRow >= 1) {
			int tabpos = view->document()->line(currentRow - 1).indexOf('&', currentCol);
			if(tabpos >= 0) {
				currentCol = tabpos;
				prefix.clear();
			}
		}
	}
	
	if(tab.isEmpty()) {
		tab = "&";
	}
	tab = prefix + tab + ' ';
	
	view->document()->insertText(KTextEditor::Cursor(currentRow, currentCol), tab);
	view->setCursorPosition(KTextEditor::Cursor(currentRow, currentCol + tab.length()));
}

//////////////////// autocomplete environment ////////////////////

// should we complete the current environment (call from KileEventFilter)

bool EditorExtension::eventInsertEnvironment(KTextEditor::View *view)
{
	// don't complete environment, if we are
	// still working inside the completion box
	if(m_complete->inProgress()) {
		return false;
	}

	int row = view->cursorPosition().line();
	int col = view->cursorPositionVirtual().column();
	QString line = view->document()->line(row).left(col);

	int pos = m_regexpEnter.search(line);
	if (pos != -1) {
		line = m_regexpEnter.cap(1);
		for(int i = 0; i < line.length(); ++i) {
			if(!line[i].isSpace()) {
				line[i] = ' ';
			}
		}

		QString envname, endenv;
		if(m_regexpEnter.cap(2) == "\\[") {
			envname = m_regexpEnter.cap(2);
			endenv = "\\]\n";
		}
		else {
			envname = m_regexpEnter.cap(4);
			endenv = m_regexpEnter.cap(2).replace("\\begin","\\end") + '\n';
		}
		
		if(shouldCompleteEnv(envname, view)) {
			QString item =  m_latexCommands->isListEnv(envname) ? "\\item " : QString();
			view->document()->insertText(KTextEditor::Cursor(row, col), '\n' + line + m_envAutoIndent + item + '\n' + line + endenv);
			view->setCursorPosition(KTextEditor::Cursor(row + 1, line.length() + m_envAutoIndent.length() + item.length()));
			return true;
		}
	}
	return false;
}

bool EditorExtension::shouldCompleteEnv(const QString &env, KTextEditor::View *view)
{
	KILE_DEBUG() << "===EditorExtension::shouldCompleteEnv( " << env << " )===";
	QRegExp reTestBegin,reTestEnd;
	if(env == "\\[") {
		KILE_DEBUG() << "display style";
		reTestBegin.setPattern("(?:[^\\\\]|^)\\\\\\["); 
		// the first part is a non-capturing bracket (?:...) and we check if we don't have a backslash in front,
		//  or that we are at the begin of the line
		reTestEnd.setPattern("(?:[^\\\\]|^)\\\\\\]");
	}
	else {
		reTestBegin.setPattern("(?:[^\\\\]|^)\\\\begin\\s*\\{" + QRegExp::escape(env) + "\\}");
		reTestEnd.setPattern("(?:[^\\\\]|^)\\\\end\\s*\\{" + QRegExp::escape(env) + "\\}");
	}

	int num = view->document()->lines();
	int numBeginsFound = 0;
	int numEndsFound = 0;
	uint realLine, realColumn;
	KTextEditor::Cursor cursor = view->cursorPosition();
	realLine = cursor.line();
	realColumn = cursor.column();

	for(int i = realLine; i < num; ++i) {
		numBeginsFound += view->document()->line(i).count(reTestBegin);
		numEndsFound += view->document()->line(i).count(reTestEnd);
		KILE_DEBUG() << "line is " << i <<  " numBeginsFound = " << numBeginsFound <<  " , " << "numEndsFound = " << numEndsFound;
		if ( numEndsFound >= numBeginsFound )
			return false;
		else if ( numEndsFound == 0 && numBeginsFound > 1 )
			return true;
		else if ( numBeginsFound > 2 || numEndsFound > 1 )
			return true; // terminate the search
	}

	return true;
}

QString EditorExtension::getWhiteSpace(const QString &s)
{
	QString whitespace = s;

	for(int i = 0; i < whitespace.length(); ++i) {
		if(!whitespace[i].isSpace()) {
			whitespace[i] = ' ';
		}
	}
	return whitespace;
}

//////////////////// inside verbatim commands ////////////////////

bool EditorExtension::insideVerbatim(KTextEditor::View *view)
{
	int rowEnv, colEnv;
	QString nameEnv;

	if(findOpenedEnvironment(rowEnv, colEnv, nameEnv, view)) {
		if(m_latexCommands->isVerbatimEnv(nameEnv)) {
			return true;
		}
	}

	return false;
}

bool EditorExtension::insideVerb(KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return false;
	}

	// get current position
	int row, col;
	KTextEditor::Cursor cursor = view->cursorPosition();
	row = cursor.line();
	col = cursor.column();

	int startpos = 0;
	QString textline = getTextLineReal(view->document(),row);
	QRegExp reg("\\\\verb(\\*?)(.)");
	while(true) {
		int pos = textline.indexOf(reg,startpos);
		if(pos < 0 || col < pos + 6 + reg.cap(1).length()) {
			return false;
		}
		pos = textline.indexOf(reg.cap(2), pos + 6 + reg.cap(1).length());
		if(pos < 0 || col <= pos) {
			return true;
		}

		startpos = pos + 1;
	}
}

//////////////////// goto sectioning command ////////////////////

void EditorExtension::gotoNextSectioning()
{
	gotoSectioning(false);
}

void EditorExtension::gotoPrevSectioning()
{
	gotoSectioning(true);
}

void EditorExtension::gotoSectioning(bool backwards, KTextEditor::View *view)
{
	view = determineView(view);
	if(!view) {
		return;
	}

	int rowFound, colFound;
	m_ki->viewManager()->updateStructure(true);
	if(m_ki->structureWidget()->findSectioning(view->document(), view->cursorPosition().line(), view->cursorPosition().column(), backwards, rowFound, colFound)) {
		view->setCursorPosition(KTextEditor::Cursor(rowFound, colFound));
	}
}

//////////////////// sectioning popup ////////////////////

void EditorExtension::sectioningCommand(KileListViewItem *item, int id)
{
	KTextEditor::View *view = determineView(NULL);
	if(!view) {
		return;
	}

	if(!item) {
		return;
	}
	KTextEditor::Document *doc = view->document();

	// try to determine the whole secting
	// get the start auf the selected sectioning
	int row, col, row1, col1, row2, col2;
	row = row1 = item->startline() - 1;
	col = col1 = item->startcol() - 1;

	// check, if the document was changed in the meantime 
	QRegExp reg( "\\\\(part|chapter|section|subsection|subsubsection|paragraph|subparagraph)\\*?\\s*(\\{|\\[)" );
	QString textline = getTextLineReal(doc,row1);
	if(reg.search(textline, col1) != col1) {
		m_ki->logWidget()->clear();
		m_ki->logWidget()->printMsg(KileTool::Error,
		       i18n("The document was modified and the structure view should be updated, before starting such an operation."),
		       i18n("Structure View Error") );
		return;
	}

	// increase cursor position and find the following sectioning command
	if(!increaseCursorPosition(doc, row, col)) {
		return;
	}
	if(!m_ki->structureWidget()->findSectioning(doc, row, col, false, row2, col2)) {
		// or the end of the document
		// if there is a '\end{document} command, we have to exclude it
		if (!findEndOfDocument(doc, row, col, row2, col2)) {
			row2 = doc->lines() - 1;
			col2 = 0;
		}
	}

	// clear selection and make cursor position visible
	view->removeSelection();
 	view->setCursorPosition(KTextEditor::Cursor(row1, col1));

	QString text;
	doc->startEditing();
	switch (id) {
		case KileWidget::Structure::SectioningCut:
			KApplication::clipboard()->setText(doc->text(KTextEditor::Range(row1, col1, row2, col2)));  // copy -> clipboard
			doc->removeText(KTextEditor::Range(row1, col1, row2, col2));                                  // delete
		break;
		case KileWidget::Structure::SectioningCopy:
			KApplication::clipboard()->setText(doc->text(KTextEditor::Range(row1, col1, row2, col2)));  // copy -> clipboard
		break;
		case KileWidget::Structure::SectioningPaste: 
			text = KApplication::clipboard()->text();                              // clipboard -> text
			if(!text.isEmpty()) {
				view->setCursorPosition(KTextEditor::Cursor(row2, col2));                             // insert
				view->insertText(text + '\n');
			}
		break;
		case KileWidget::Structure::SectioningSelect:
			view->setSelection(KTextEditor::Range(row1, col1, row2, col2));                                // select
		break;
		case KileWidget::Structure::SectioningDelete:
			doc->removeText(KTextEditor::Range(row1, col1, row2, col2));                                  // delete
		break;
		case KileWidget::Structure::SectioningComment:
			view->setSelection(KTextEditor::Range(row1, col1, row2, col2));
#ifdef __GNUC__
#warning The 'comment' feature still needs to be ported somehow!
#endif
//FIXME: port for KDE4
// 			view->comment();
			view->removeSelection();
		break;
		case KileWidget::Structure::SectioningPreview: 
			view->setSelection(KTextEditor::Range(row1, col1, row2, col2));                               // quick preview
			m_ki->quickPreview()->previewSelection(view, false);
			view->removeSelection();
		break;
	}
	doc->endEditing();

	// update structure view, because it has changed
	if(id == KileWidget::Structure::SectioningDelete || id == KileWidget::Structure::SectioningComment) {
		m_ki->viewManager()->updateStructure(true);
	}

}

#ifdef __GNUC__
#warning Redesign the findEndOfDocument method (line 2795)!
#endif
//FIXME: fix for KDE4
bool EditorExtension::findEndOfDocument(KTextEditor::Document *doc, int row, int col, int &rowFound, int &colFound)
{
/*
	KTextEditor::SearchInterface *iface;
	iface = dynamic_cast<KTextEditor::SearchInterface *>(doc);	

	uint lenFound;
	QString textline;
	KTextEditor::Range documentRange(KTextEditor::Cursor(row, col), doc->documentEnd());
	QVector<KTextEditor::Range> foundRanges = iface->searchText(documentRange, "\\end{document}");
	for(QVector<KTextEditor::Range>::iterator i = foundRanges.begin(); i != foundRanges.end(); ++i)
// 	while ( iface->searchText(row,col,"\\end{document}",&rowFound,&colFound,&lenFound) )
	{
		rowFound = 
		textline = getTextLineReal(doc, rowFound);
		if ( textline.indexOf("\\end{document}",colFound) == (int)colFound )
			return true;

		row = rowFound;
		col = colFound;
		if ( ! increaseCursorPosition(doc,row,col) )
			break;
	}
*/
	return false;
}

}

#include "editorextension.moc"