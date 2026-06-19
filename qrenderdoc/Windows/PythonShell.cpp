/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2017-2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "PythonShell.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QStringListModel>
#include <QTimer>
#include "Code/QRDUtils.h"
#include "Code/Resources.h"
#include "Code/ScintillaSyntax.h"
#include "Code/pyrenderdoc/PythonContext.h"
#include "Widgets/Extended/RDToolTip.h"
#include "Widgets/FindReplace.h"
#include "scintilla/include/SciLexer.h"
#include "toolwindowmanager/ToolWindowManagerArea.h"
#include "ui_PythonShell.h"

enum
{
  AllOutputFilter,
  ScriptOutputFilter,
  FirstExtensionOutputFilter,
};

void updateEditorTitle(ScintillaEdit *editor);

void setEditorFilename(ScintillaEdit *editor, QString filename)
{
  if(!editor)
    return;

  QObject *obj = (QObject *)editor;
  obj->setProperty("filename", filename);

  ToolWindowManager *manager = ToolWindowManager::managerOf(editor);

  ToolWindowManagerArea *editorTabs = manager->areaOf(editor);
  int idx = editorTabs->indexOf(editor);
  if(idx >= 0)
    editorTabs->setTabToolTip(idx, filename);

  updateEditorTitle(editor);
}

QString getEditorFilename(ScintillaEdit *editor)
{
  if(!editor)
    return QString();
  QObject *obj = (QObject *)editor;
  return obj->property("filename").toString();
}

void markEditorModified(ScintillaEdit *editor, bool modified)
{
  if(!editor)
    return;

  QObject *obj = (QObject *)editor;
  obj->setProperty("modified", modified);

  updateEditorTitle(editor);
}

bool isEditorModified(ScintillaEdit *editor)
{
  if(!editor)
    return false;

  QObject *obj = (QObject *)editor;
  return obj->property("modified").toBool();
}

void updateEditorTitle(ScintillaEdit *editor)
{
  if(!editor)
    return;

  QString title;
  QString filename = getEditorFilename(editor);
  if(filename.isEmpty())
  {
    if(isEditorModified(editor))
      editor->setWindowTitle(lit("Untitled Script *"));
    else
      editor->setWindowTitle(lit("Untitled Script"));
  }
  else
  {
    if(isEditorModified(editor))
      editor->setWindowTitle(QFileInfo(filename).fileName() + lit(" *"));
    else
      editor->setWindowTitle(QFileInfo(filename).fileName());
  }
}

EditorWrapper::EditorWrapper(PythonShell *parent) : ScintillaEdit(parent), m_PyShell(parent)
{
}

bool EditorWrapper::checkAllowClose()
{
  if(isEditorModified(this))
  {
    QString filename = getEditorFilename(this);
    bool untitled = false;
    if(filename.isEmpty())
    {
      untitled = true;
      filename = lit("Untitled Script");
    }
    QMessageBox::StandardButton res = RDDialog::question(this, tr("Python script is modified"),
                                                         tr("You have unsaved changes to '%1'.\n"
                                                            "Do you want to save them?")
                                                             .arg(QFileInfo(filename).fileName()),
                                                         RDDialog::YesNoCancel);

    if(res == QMessageBox::Cancel)
      return false;

    if(res == QMessageBox::No)
      return true;

    if(untitled)
      return m_PyShell->saveEditorAs(this);
    else
      return m_PyShell->saveEditor(this, filename);
  }

  return true;
}

// See PythonInvokers.cpp
ICaptureContext *MakeCaptureContextInvoker(PythonShell *shell, ICaptureContext &ctx);
void FreeCaptureContextInvoker(ICaptureContext *ctx);

PythonShell::PythonShell(ICaptureContext &ctx, QWidget *parent)
    : QFrame(parent), ui(new Ui::PythonShell), m_Ctx(ctx)
{
  ui->setupUi(this);

  m_ThreadCtx = MakeCaptureContextInvoker(this, m_Ctx);

  QObject::connect(ui->lineInput, &RDLineEdit::keyPress, this, &PythonShell::interactive_keypress);
  QObject::connect(ui->helpSearch, &RDLineEdit::keyPress, this, &PythonShell::helpSearch_keypress);

  QObject::connect(ui->lineInput, &RDLineEdit::leave, [this]() { hideFunccompleteTooltip(); });

  // we create this up front so its state stays persistent as much as possible.
  m_FindReplace = new FindReplace(m_Scintillas, this);
  m_FindReplace->allowFindAll(false);
  m_FindReplace->setDockManager(ui->docking);

  {
    m_FindResults = new ScintillaEdit(this);

    m_FindResults->styleSetFont(STYLE_DEFAULT, Formatter::FixedFont().family().toUtf8().data());
    m_FindResults->styleSetFont(100, Formatter::FixedFont().family().toUtf8().data());
    m_FindResults->styleSetBack(
        100, IsDarkTheme() ? SCINTILLA_COLOUR(175, 70, 70) : SCINTILLA_COLOUR(255, 150, 150));

    ConfigureSyntax(m_FindResults, SCLEX_NULL);
    m_FindResults->usePopUp(SC_POPUP_NEVER);
    m_FindResults->setWrapMode(SC_WRAP_WORD);
    m_FindResults->setReadOnly(true);
    m_FindResults->setWindowTitle(lit("Find Results"));
  }

  m_FindReplace->setFindIndicator(2);
  m_FindReplace->setFindAllResultsDisplay(m_FindResults);

  ui->lineInput->setFont(Formatter::FixedFont());
  ui->interactiveOutput->setFont(Formatter::FixedFont());
  ui->scriptOutput->setFont(Formatter::FixedFont());
  ui->helpText->setFont(Formatter::FixedFont());

  ui->outputGroup->setWindowTitle(tr("Output"));
  ui->helpGroup->setWindowTitle(tr("Help"));
  ui->replGroup->setWindowTitle(tr("Interactive REPL"));

  ui->lineInput->setAcceptTabCharacters(true);

  // don't repeatedly re-parse for errors. Have a reasonable timeout
  m_SyntaxCheckTimer = new QTimer(this);
  m_SyntaxCheckTimer->setSingleShot(true);
  m_SyntaxCheckTimer->setInterval(1200);

  // only update the current line intermittently. We don't need to update every single time and if
  // there is a large number of traces this will rate limit it.
  m_CurLineTimer = new QTimer(this);
  m_CurLineTimer->setSingleShot(false);
  m_CurLineTimer->setInterval(10);

  QObject::connect(m_CurLineTimer, &QTimer::timeout, [this]() {
    if(m_CurLineDirty)
    {
      if(runningScriptEditor)
      {
        runningScriptEditor->markerDeleteAll(CURRENT_MARKER);
        runningScriptEditor->markerDeleteAll(CURRENT_MARKER + 1);

        runningScriptEditor->markerAdd(m_CurLine > 0 ? m_CurLine - 1 : 0, CURRENT_MARKER);
        runningScriptEditor->markerAdd(m_CurLine > 0 ? m_CurLine - 1 : 0, CURRENT_MARKER + 1);
      }

      m_CurLineDirty = false;
    }
  });

  completionContext = new PythonContext();
  setGlobals(completionContext);

  // if we're help printing in the completion context, append it to the help text
  QObject::connect(completionContext, &PythonContext::textOutput,
                   [this](const QString &, bool isStdError, const QString &output) {
                     if(m_HelpPrinting)
                       appendText(ui->helpText, output);
                   });

  QObject::connect(m_SyntaxCheckTimer, &QTimer::timeout, this, &PythonShell::doSyntaxCheck);

  QObject::connect(ui->interactiveOutput, &RDTextEdit::keyPress, [this](QKeyEvent *e) {
    // ignore keypresses that aren't typing, but for up/down redirect that to the line input to get history
    if((e->text().isEmpty() || !e->text()[0].isPrint()) && e->key() != Qt::Key_Up &&
       e->key() != Qt::Key_Down)
      return;
    ui->lineInput->setFocus(Qt::OtherFocusReason);
    QApplication::postEvent(ui->lineInput, new QKeyEvent(*e));
  });

  m_ToolTip = new RDToolTip(this);

  m_ToolTip->installEventFilter(this);

  m_ToolTip->setFont(Formatter::FixedFont());

  m_InteractiveCompleter = new QCompleter(this);
  m_InteractiveCompleter->popup()->setFont(Formatter::FixedFont());
  m_InteractiveCompleter->popup()->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
  m_InteractiveCompleter->popup()->setTextElideMode(Qt::ElideNone);
  m_InteractiveCompleter->setWidget(ui->lineInput);
  m_InteractiveCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
  m_InteractiveCompleter->setWrapAround(false);
  m_InteractiveCompletionModel = new QStringListModel(this);
  m_InteractiveCompleter->setModel(m_InteractiveCompletionModel);
  m_InteractiveCompleter->setCompletionRole(Qt::DisplayRole);

  QObject::connect(m_InteractiveCompleter,
                   OverloadedSlot<const QModelIndex &>::of(&QCompleter::activated),
                   [this](const QModelIndex &idx) {
                     int i = idx.row();
                     if(i >= 0 && i < m_InteractiveCompletionModel->rowCount())
                     {
                       QString curText = ui->lineInput->text();
                       curText.resize(curText.size() - m_InteractiveCompletionPrefix);
                       curText += m_InteractiveCompletionModel->stringList()[i];
                       ui->lineInput->setText(curText);

                       ui->lineInput->setCursorPosition(curText.size());
                     }
                   });

  // reset output to default
  on_clear_clicked();
  on_newScript_clicked();

  ui->saveScript->setEnabled(false);

  setupTabs();

  ui->docking->addToolWindow(m_FindReplace, ToolWindowManager::NoArea);
  ui->docking->setToolWindowProperties(m_FindReplace, ToolWindowManager::HideOnClose);

  ui->docking->addToolWindow(m_FindResults, ToolWindowManager::NoArea);
  ui->docking->setToolWindowProperties(
      m_FindResults, ToolWindowManager::HideOnClose | ToolWindowManager::DisallowFloatWindow);

  ui->docking->addToolWindow(
      ui->replGroup, ToolWindowManager::AreaReference(ToolWindowManager::BottomOf,
                                                      ui->docking->areaOf(m_Scintillas[0]), 0.3f));
  ui->docking->setToolWindowProperties(
      ui->replGroup, ToolWindowManager::HideCloseButton | ToolWindowManager::DisallowFloatWindow);

  ui->projectExplorer->setWindowTitle(tr("Project Explorer"));
  ui->projectExplorer->setColumns({tr("Name")});
  ui->projectExplorer->hideGridLines();

  m_Watcher = new QFileSystemWatcher({}, this);

  QObject::connect(m_Watcher, &QFileSystemWatcher::fileChanged, this, &PythonShell::openFileModified);
  QObject::connect(m_Watcher, &QFileSystemWatcher::directoryChanged, this,
                   &PythonShell::updateExtensionProjects);

  ui->projectExplorer->beginUpdate();

  m_Examples = new RDTreeWidgetItem({lit("Examples")});
  m_Examples->setSelectable(false);
  m_Examples->setBold(true);
  m_Examples->setIcon(0, Icons::help());

  const QPair<QString, QString> examples[] = {
      {tr("Show a Buffer"), lit("Example will go here")},
  };

  for(const QPair<QString, QString> &example : examples)
  {
    RDTreeWidgetItem *ex = new RDTreeWidgetItem({example.first});

    QFile file(example.second);

    file.open(QFile::ReadOnly);
    ex->setData(0, Qt::UserRole, QString::fromUtf8(file.readAll()));
    file.close();

    m_Examples->addChild(ex);
  }

  m_UIExtensions = new RDTreeWidgetItem({lit("UI Extensions")});
  m_UIExtensions->setSelectable(false);
  m_UIExtensions->setBold(true);
  m_UIExtensions->setIcon(0, Icons::plugin());

  m_RecentFiles = new RDTreeWidgetItem({lit("Recent files")});
  m_RecentFiles->setSelectable(false);
  m_RecentFiles->setBold(true);
  m_RecentFiles->setIcon(0, Icons::page_white_edit());

  ui->projectExplorer->addTopLevelItem(m_Examples);
  ui->projectExplorer->addTopLevelItem(m_UIExtensions);
  ui->projectExplorer->addTopLevelItem(m_RecentFiles);

  updateRecentFiles(false);
  updateExtensionProjects();

  ui->projectExplorer->endUpdate();

  ui->projectExplorer->expandItem(m_RecentFiles);

  ui->docking->addToolWindow(ui->projectExplorer, ToolWindowManager::AreaReference(
                                                      ToolWindowManager::LeftOf,
                                                      ui->docking->areaOf(m_Scintillas[0]), 0.2f));
  ui->docking->setToolWindowProperties(
      ui->projectExplorer,
      ToolWindowManager::HideCloseButton | ToolWindowManager::DisallowFloatWindow);

  ui->docking->addToolWindow(ui->outputGroup,
                             ToolWindowManager::AreaReference(ToolWindowManager::AddTo,
                                                              ui->docking->areaOf(ui->replGroup)));
  ui->docking->setToolWindowProperties(
      ui->outputGroup, ToolWindowManager::HideCloseButton | ToolWindowManager::DisallowFloatWindow);

  ui->docking->addToolWindow(ui->helpGroup,
                             ToolWindowManager::AreaReference(ToolWindowManager::AddTo,
                                                              ui->docking->areaOf(ui->replGroup)));
  ui->docking->setToolWindowProperties(
      ui->helpGroup, ToolWindowManager::HideCloseButton | ToolWindowManager::DisallowFloatWindow);

  ToolWindowManager::raiseToolWindow(ui->replGroup);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setSpacing(3);
  layout->setContentsMargins(3, 3, 3, 3);
  layout->addWidget(ui->toolbar);
  layout->addWidget(ui->docking);

  enableButtons(true);

  Q_ASSERT(ui->outputContext->count() == AllOutputFilter);
  ui->outputContext->addItem(tr("All"));
  Q_ASSERT(ui->outputContext->count() == ScriptOutputFilter);
  ui->outputContext->addItem(tr("Script"));
  Q_ASSERT(ui->outputContext->count() == FirstExtensionOutputFilter);

  for(const ExtensionMetadata &e : m_Ctx.Extensions().GetInstalledExtensions())
  {
    if(m_Ctx.Extensions().IsExtensionLoaded(e.package))
    {
      ui->outputContext->addItem(tr("Extension %1").arg(e.package));
      loadedExtensions.push_back(e.package);
    }
  }

  QObject::connect(PythonContext::GetExtensionContext(), &PythonContext::textOutput, this,
                   &PythonShell::textOutput);
  QObject::connect(PythonContext::GetExtensionContext(), &PythonContext::exception, this,
                   &PythonShell::exception);
  QObject::connect(PythonContext::GetExtensionContext(), &PythonContext::extensionLoaded, this,
                   &PythonShell::extensionLoaded);

  m_Ctx.GetMainWindow()->RegisterShortcut("CTRL+S", this,
                                          [this](QWidget *) { this->on_saveScript_clicked(); });

  // we defer debugging loading onto a thread so check after a delay
  QTimer::singleShot(1200, [this]() {
    if(!PythonContext::IsDebuggingEnabled())
    {
      ui->debugScript->setEnabled(false);
      ui->debugScript->setToolTip(
          tr("Debugging not supported - check documentation for setup instructions"));
    }
  });
}

PythonShell::~PythonShell()
{
  m_Ctx.BuiltinWindowClosed(this);

  m_Ctx.GetMainWindow()->UnregisterShortcut("CTRL+S", this);

  // on a clean shutdown, remove the unsaved temp file
  QString unsavedFile = interactiveContext->GetTempFilename(lit("script.py"));

  if(!unsavedFile.isEmpty() && QFile::exists(unsavedFile))
    QFile::remove(unsavedFile);

  for(ScintillaEdit *edit : m_Scintillas)
    delete edit;

  delete m_ToolTip;

  completionContext->Finish();
  interactiveContext->Finish();

  FreeCaptureContextInvoker(m_ThreadCtx);

  delete ui;
}

void PythonShell::doSyntaxCheck()
{
  ScintillaEdit *editor = curEditor();

  if(!editor)
    return;

  if(editor->lexer() != SCLEX_PYTHON)
    return;

  // don't syntax check while the user still seems to be editing, e.g. with autocomplete or a
  // function tooltip active. The syntax check timer will be restarted when these go away
  if(editor->autoCActive() || m_FuncTip)
    return;

  QByteArray script = editor->getText(editor->textLength() + 1);
  PyParseError parseError = completionContext->CheckPyParse(script, "script.py");

  if(parseError.lineno >= 0)
  {
    sptr_t end = editor->lineLength(parseError.lineno - 1);
    sptr_t linePos = editor->positionFromLine(parseError.lineno - 1);
    while(QChar(QLatin1Char(script[int(linePos + end - 1)])).isSpace())
      end--;
    editor->setIndicatorCurrent(0);
    editor->indicatorFillRange(linePos + parseError.offset - 1, end + 1 - parseError.offset);

    editor->annotationSetText(parseError.lineno - 1, parseError.errStr.c_str());
    editor->annotationSetVisible(ANNOTATION_BOXED);
    editor->annotationSetStyle(parseError.lineno - 1, 100);
  }
}

void PythonShell::editorTab_Changed(int index)
{
  ScintillaEdit *editor = curEditor();

  ui->saveScript->setEnabled(getEditorFilename(editor) != QString());
}

void PythonShell::openFileModified(const QString &path)
{
  for(ScintillaEdit *edit : m_Scintillas)
  {
    if(getEditorFilename(edit) == path)
    {
      // delay slightly to avoid reading while the file is being written or if it was deleted before
      // being written as some editors do
      QTimer::singleShot(150, [this, edit, path]() {
        bool mod = isEditorModified(edit);

        // re-add the path, it may have been removed if the file was deleted
        m_Watcher->addPath(path);

        if(!mod && !m_Ctx.Config().Python_PromptReloadUnchanged)
        {
          // unconditionally reload
        }
        else
        {
          QString prompt = tr("Reload from disk and overwrite your changes?");
          if(!mod)
            prompt = tr("Reload from disk?");

          QMessageBox::StandardButton response = RDDialog::question(
              this, tr("%1 has been modified on disk").arg(QFileInfo(path).fileName()),
              tr("%1 has been modified on disk. %2").arg(QFileInfo(path).fileName()).arg(prompt));

          if(response == QMessageBox::No)
            return;
        }

        QFile f(path);
        if(f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
          edit->setText(f.readAll().data());
          markEditorModified(edit, false);
          return;
        }
      });
    }
  }
}

void PythonShell::editorTab_Menu(const QPoint &pos)
{
  ToolWindowManagerArea *editorTabs = ui->docking->areaOf(m_Scintillas[0]);

  int tabIndex = editorTabs->tabBar()->tabAt(pos);

  if(tabIndex == -1)
    return;

  if(editorTabs->count() == 1)
    return;

  QAction closeTab(tr("Close tab"), this);
  QAction closeOtherTabs(tr("Close other tabs"), this);
  QAction closeRightTabs(tr("Close tabs to the right"), this);

  QMenu contextMenu(this);

  contextMenu.addAction(&closeTab);
  contextMenu.addAction(&closeOtherTabs);
  contextMenu.addAction(&closeRightTabs);

  QObject::connect(&closeTab, &QAction::triggered, [this, editorTabs, tabIndex]() {
    // remove the tab at this index
    delete editorTabs->widget(tabIndex);
  });

  QObject::connect(&closeRightTabs, &QAction::triggered, [this, editorTabs, tabIndex]() {
    for(int i = editorTabs->count() - 1; i > tabIndex; i--)
      delete editorTabs->widget(i);
  });

  QObject::connect(&closeOtherTabs, &QAction::triggered, [this, editorTabs, tabIndex]() {
    for(int i = editorTabs->count() - 1; i >= 0; i--)
      if(i != tabIndex)
        delete editorTabs->widget(i);
  });

  RDDialog::show(&contextMenu, QCursor::pos());
}

void PythonShell::updateExtensionProjects()
{
  RDTreeViewExpansionState expansion;
  ui->projectExplorer->saveExpansion(expansion, 0);

  ui->projectExplorer->beginUpdate();

  m_UIExtensions->clear();

  for(const ExtensionMetadata &ext : m_Ctx.Extensions().GetInstalledExtensions())
  {
    RDTreeWidgetItem *root = new RDTreeWidgetItem({ext.name});

    addExtensionDirItems(root, QDir(ext.filePath));

    m_UIExtensions->addChild(root);
  }

  ui->projectExplorer->endUpdate();

  ui->projectExplorer->applyExpansion(expansion, 0);
}

void PythonShell::addExtensionDirItems(RDTreeWidgetItem *root, QDir dir)
{
  m_Watcher->addPath(dir.absolutePath());
  for(QString child : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
  {
    QString path = dir.absoluteFilePath(child);
    QFileInfo fileInfo(path);

    // only show .py files, .md files (for README.md) and the extension.json
    if(fileInfo.isFile() && fileInfo.suffix().toLower() != lit("py") &&
       fileInfo.suffix().toLower() != lit("md") && child.toLower() != lit("extension.json"))
      continue;

    RDTreeWidgetItem *item = new RDTreeWidgetItem({child});
    if(fileInfo.isDir())
    {
      // ignore some directories
      if(child.toLower() == lit("__pycache__") || child.toLower() == lit(".git") ||
         child.toLower() == lit(".vscode"))
        continue;

      item->setIcon(0, Icons::folder());

      addExtensionDirItems(item, QDir(path));
    }
    else
    {
      item->setData(0, Qt::UserRole, path);
    }

    root->addChild(item);
  }
}

ScintillaEdit *PythonShell::curEditor()
{
  for(ScintillaEdit *edit : m_Scintillas)
  {
    if(edit->isVisible())
      return edit;
  }

  return NULL;
}

ScintillaEdit *PythonShell::makeEditor(rdcstr filename)
{
  ScintillaEdit *editor = new EditorWrapper(this);

  editor->indicSetFore(0, 0x0000ff);

  m_FindReplace->configureFindIndicator(editor);

  editor->styleSetFont(STYLE_DEFAULT, Formatter::FixedFont().family().toUtf8().data());
  editor->styleSetFont(100, Formatter::FixedFont().family().toUtf8().data());
  editor->styleSetBack(
      100, IsDarkTheme() ? SCINTILLA_COLOUR(175, 70, 70) : SCINTILLA_COLOUR(255, 150, 150));

  editor->setMarginLeft(4.0);
  editor->setMarginWidthN(0, 32.0);
  editor->setMarginWidthN(1, 0.0);
  editor->setMarginWidthN(2, 16.0);
  editor->setObjectName(lit("scriptEditor"));

  editor->markerSetBack(CURRENT_MARKER, SCINTILLA_COLOUR(240, 128, 128));
  editor->markerSetBack(CURRENT_MARKER + 1, SCINTILLA_COLOUR(240, 128, 128));
  editor->markerDefine(CURRENT_MARKER, SC_MARK_SHORTARROW);
  editor->markerDefine(CURRENT_MARKER + 1, SC_MARK_BACKGROUND);

  editor->usePopUp(SC_POPUP_NEVER);

  editor->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(editor, &ScintillaEdit::customContextMenuRequested, this,
                   &PythonShell::editor_contextMenu);

  QString suffix;

  if(!filename.isEmpty())
    suffix = QFileInfo(filename).suffix().toLower();
  if(suffix == lit("md"))
  {
    ConfigureSyntax(editor, SCLEX_NULL);
    editor->setWrapMode(SC_WRAP_WORD);
  }
  else if(suffix.toLower() == lit("json"))
  {
    ConfigureSyntax(editor, SCLEX_JSON);
    editor->setWrapMode(SC_WRAP_WORD);
  }
  else
  {
    ConfigureSyntax(editor, SCLEX_PYTHON);
  }

  editor->setTabWidth(4);
  editor->setUseTabs(false);

  editor->setScrollWidth(1);
  editor->setScrollWidthTracking(true);

  editor->colourise(0, -1);

  editor->autoCSetMaxHeight(10);
  editor->autoCSetCancelAtStart(false);

  editor->setMouseDwellTime(400);

  editor->installEventFilter(this);

  QObject::connect(editor, &QWidget::destroyed, [this, editor]() {
    hideFunccompleteTooltip();
    m_Scintillas.removeOne(editor);
    m_Watcher->removePath(getEditorFilename(editor));
    updateEditorCloseButton();
  });

  // start syntax checking if we exit autocomplete
  QObject::connect(editor, &ScintillaEdit::autoCompleteCancelled,
                   [this]() { m_SyntaxCheckTimer->start(); });
  QObject::connect(editor, &ScintillaEdit::autoCompleteSelection,
                   [this]() { m_SyntaxCheckTimer->start(); });

  QObject::connect(editor, &ScintillaEdit::modified,
                   [this, editor](int type, int, int, int, const QByteArray &text, int, int, int) {
                     if(type & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT | SC_MOD_BEFOREINSERT |
                                SC_MOD_BEFOREDELETE))
                     {
                       m_FindReplace->clearFindState();

                       markEditorModified(editor, true);
                       updateEditorTitle(editor);

                       editor->markerDeleteAll(CURRENT_MARKER);
                       editor->markerDeleteAll(CURRENT_MARKER + 1);

                       // always remove errors immediately
                       editor->setIndicatorCurrent(0);
                       editor->indicatorClearRange(0, editor->textLength());
                       editor->annotationClearAll();

                       m_SyntaxCheckTimer->start();
                     }

                     if(type & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))
                     {
                       if(!editor->autoCActive() || text.contains('\r') || text.contains('\n'))
                       {
                         completionContext->reflectSource(
                             QString::fromUtf8(editor->getText(editor->textLength() + 1)));
                       }
                       else if(editor->autoCActive())
                       {
                         // delay updating the autocomplete so the current cursor position is updated
                         GUIInvoke::defer(this, [this, editor]() { doAutocomplete(editor); });
                       }
                     }
                   });

  QObject::connect(editor, &ScintillaEdit::dwellStart, [this, editor](int x, int y) {
    if(editor->autoCActive())
      return;

    if(m_ToolTip->isVisible() && m_FuncTip)
      return;

    if(!editor->geometry().contains(editor->mapFromGlobal(QCursor::pos())))
      return;

    QWidget *widgetInCursor = QApplication::widgetAt(QCursor::pos());
    if(widgetInCursor && editor != widgetInCursor && editor != widgetInCursor->parentWidget())
      return;

    sptr_t pos = editor->positionFromPointClose(x, y);

    if(pos == -1)
      return;

    sptr_t line = editor->lineFromPosition(pos);
    sptr_t col = pos - editor->positionFromLine(line);

    QString tooltip = completionContext->tooltipForLoc(line + 1, col);

    if(!tooltip.isEmpty())
    {
      hideFunccompleteTooltip();

      m_ToolTip->configureTip(this, tooltip);
      m_ToolTip->showTipAtPos(QCursor::pos() + QPoint(5, 5));
    }
  });

  QObject::connect(editor, &ScintillaEdit::dwellEnd, [this, editor](int, int) {
    if(editor->autoCActive())
      return;

    if(!m_FuncTip)
      m_ToolTip->hideTip();
  });

  QObject::connect(editor, &ScintillaEdit::charAdded,
                   [this, editor](int ch) { doAutocomplete(editor); });

  QObject::connect(editor, &ScintillaEdit::buttonPressed,
                   [this, editor](QMouseEvent *ev) { hideFunccompleteTooltip(); });

  QObject::connect(editor, &ScintillaEdit::keyPressed, [this, editor](QKeyEvent *ev) {
    if(ev->key() == Qt::Key_Space && (ev->modifiers() & Qt::ControlModifier))
      doAutocomplete(editor);

    if(m_ToolTip->isVisible() && m_FuncTip)
    {
      if(editor->lineFromPosition(editor->currentPos()) == m_FuncTipLine)
      {
        doFunccomplete(editor);
        return;
      }

      hideFunccompleteTooltip();
    }

    if(ev->key() == Qt::Key_F1)
    {
      sptr_t pos = editor->currentPos();

      if(pos >= 0)
      {
        sptr_t line = editor->lineFromPosition(pos);
        sptr_t col = pos - editor->positionFromLine(line);

        QString typeName = completionContext->typenameForLoc(line + 1, col);

        if(!typeName.isEmpty())
          selectedHelp(typeName);
      }
    }

    m_FindReplace->handleEditorKeypress(editor, ev);
  });

  if(m_Scintillas.empty())
  {
    ui->docking->addToolWindow(editor, ToolWindowManager::EmptySpace);
  }
  else
  {
    ui->docking->addToolWindow(
        editor, ToolWindowManager::AreaReference(ToolWindowManager::AddTo,
                                                 ui->docking->areaOf(m_Scintillas[0])));
  }

  m_Scintillas.push_back(editor);

  updateEditorCloseButton();

  return editor;
}

void PythonShell::updateEditorCloseButton()
{
  for(ScintillaEdit *edit : m_Scintillas)
  {
    ToolWindowManager::ToolWindowProperty props =
        ToolWindowManager::DisallowUserDocking | ToolWindowManager::AlwaysDisplayFullTabs;

    // disallow closing last scintilla
    if(m_Scintillas.size() == 1)
      props = props | ToolWindowManager::HideCloseButton;

    ui->docking->setToolWindowProperties(edit, props);
  }
}

void PythonShell::addRecentFile(rdcstr filename)
{
  // don't add recent files from UI extensions
  for(const ExtensionMetadata &ext : m_Ctx.Extensions().GetInstalledExtensions())
  {
    if(QString(QFileInfo(filename).absolutePath())
           .toLower()
           .startsWith(QDir(ext.filePath).absolutePath().toLower()))
    {
      return;
    }
  }

  m_Ctx.Config().Python_RecentFiles.removeIf([filename](const rdcstr &o) { return o == filename; });

  if(m_Ctx.Config().Python_RecentFiles.size() == 10)
    m_Ctx.Config().Python_RecentFiles.pop_back();

  m_Ctx.Config().Python_RecentFiles.insert(0, filename);

  m_Ctx.Config().Save();

  updateRecentFiles(true);
}

void PythonShell::updateRecentFiles(bool added)
{
  ui->projectExplorer->beginUpdate();

  bool expanded = ui->projectExplorer->isItemExpanded(m_RecentFiles);

  // expand if we're adding the first recent file
  if(m_Ctx.Config().Python_RecentFiles.size() == 1 && added)
    expanded = true;

  m_RecentFiles->clear();

  QString unsavedFile = interactiveContext->GetTempFilename(lit("script.py"));

  if(!unsavedFile.isEmpty() && QFile::exists(unsavedFile) && !m_IgnoreRecovered)
  {
    RDTreeWidgetItem *recent = new RDTreeWidgetItem({tr("Recovered Script")});
    recent->setItalic(true);
    recent->setData(0, Qt::UserRole, QString(unsavedFile));
    m_RecentFiles->addChild(recent);
  }
  else
  {
    // we didn't have a recovered script, remember that and don't display the file in future
    // refreshes appears in future due to temp script running. When the window is closed the script
    // will be removed, and if we crash and restart then the script will be found
    m_IgnoreRecovered = true;
  }

  for(rdcstr file : m_Ctx.Config().Python_RecentFiles)
  {
    RDTreeWidgetItem *recent = new RDTreeWidgetItem({QFileInfo(file).fileName()});
    recent->setData(0, Qt::UserRole, QString(file));
    m_RecentFiles->addChild(recent);
  }

  ui->projectExplorer->endUpdate();

  if(expanded)
    ui->projectExplorer->expandItem(m_RecentFiles);
  else
    ui->projectExplorer->collapseItem(m_RecentFiles);
}

bool PythonShell::eventFilter(QObject *watched, QEvent *event)
{
  if(qobject_cast<ScintillaEdit *>(watched))
  {
    if(event->type() == QEvent::Leave)
    {
      if(!m_FuncTip)
      {
        m_ToolTip->hideTip();
      }
      else if(m_FuncTip)
      {
        if(!m_ToolTip->geometry().contains(QCursor::pos()))
        {
          hideFunccompleteTooltip();
        }
      }
    }
    else if(event->type() == QEvent::KeyPress && ((QKeyEvent *)event)->key() == Qt::Key_Escape)
    {
      hideFunccompleteTooltip();
    }
  }

  if(m_FuncTip && watched == m_FuncTipWidget && event->type() == QEvent::FocusOut)
  {
    hideFunccompleteTooltip();
  }

  return QObject::eventFilter(watched, event);
}

QVariant PythonShell::persistData()
{
  QVariantMap state = ui->docking->saveState();

  RDTreeViewExpansionState expansion;
  ui->projectExplorer->saveExpansion(expansion, 0);

  QVariantList expansionList;
  for(uint x : expansion)
    expansionList.append(x);
  state[lit("projectExplorerExpansion")] = expansionList;

  return state;
}

void PythonShell::setPersistData(const QVariant &persistData)
{
  QVariantMap state = persistData.toMap();

  ui->docking->restoreState(state);

  ui->projectExplorer->collapseAll();

  RDTreeViewExpansionState expansion;
  for(QVariant x : state[lit("projectExplorerExpansion")].toList())
    expansion.insert(x.toUInt());
  ui->projectExplorer->applyExpansion(expansion, 0);

  setupTabs();
}

void PythonShell::setupTabs()
{
  ToolWindowManagerArea *editorTabs = ui->docking->areaOf(m_Scintillas[0]);
  QObject::connect(editorTabs, &QTabWidget::currentChanged, this, &PythonShell::editorTab_Changed);

  editorTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);

  QObject::connect(editorTabs->tabBar(), &QTabBar::customContextMenuRequested, this,
                   &PythonShell::editorTab_Menu);
}

PythonContext *PythonShell::GetScriptContext()
{
  return scriptContext;
}

bool PythonShell::CheckUnsavedChanges()
{
  return checkAllowClose();
}

bool PythonShell::LoadScriptFromFilename(rdcstr filename)
{
  if(!filename.isEmpty())
  {
    QFile f(filename);
    if(f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      ScintillaEdit *editor = makeEditor(filename);

      editor->setText(f.readAll().data());
      editor->emptyUndoBuffer();

      ui->saveScript->setEnabled(true);

      QString unsavedFile = interactiveContext->GetTempFilename(lit("script.py"));
      if(QString(filename) != unsavedFile)
        addRecentFile(filename);

      m_Watcher->addPath(filename);

      setEditorFilename(editor, QString(filename));
      markEditorModified(editor, false);
      return true;
    }
  }

  return false;
}

void PythonShell::CreateNewScriptEditor(rdcstr name, rdcstr text)
{
  ScintillaEdit *editor = makeEditor("");

  editor->setText(text.c_str());
  editor->emptyUndoBuffer();

  ui->saveScript->setEnabled(true);

  setEditorFilename(editor, QString(name));
  markEditorModified(editor, false);
}

rdcstr PythonShell::GetScriptText()
{
  ScintillaEdit *editor = curEditor();

  if(!editor)
    return rdcstr();

  return editor->getText(editor->textLength() + 1).data();
}

void PythonShell::SetExtensionOutputFilter(const rdcstr &extensionName)
{
  if(extensionName.empty())
    return;

  int idx = loadedExtensions.indexOf(extensionName);
  if(idx < 0)
    return;

  ui->outputContext->setCurrentIndex(FirstExtensionOutputFilter + idx);
}

void PythonShell::SetScriptOutputFilter()
{
  ui->outputContext->setCurrentIndex(ScriptOutputFilter);
}

void PythonShell::RemoveOutputFilter()
{
  ui->outputContext->setCurrentIndex(AllOutputFilter);
}

void PythonShell::ShowOutput()
{
  ToolWindowManager::raiseToolWindow(ui->outputGroup);
}

void PythonShell::ShowREPL()
{
  ToolWindowManager::raiseToolWindow(ui->replGroup);
}

void PythonShell::ShowHelp()
{
  ToolWindowManager::raiseToolWindow(ui->helpGroup);
}

void PythonShell::runScript(bool debugging)
{
  ScintillaEdit *editor = curEditor();

  if(!editor)
    return;

  PythonContext *context = newContext();

  ANALYTIC_SET(UIFeatures.PythonInterop, true);

  ShowOutput();

  scriptOutputLines.removeIf([](const ScriptOutputLine &l) { return l.extension.isEmpty(); });

  updateScriptOutput(true);

  QString script = QString::fromUtf8(editor->getText(editor->textLength() + 1));

  enableButtons(false);

  if(debugging)
    PythonContext::PrepareDebuggerWait();

  m_CurLineTimer->start();

  LambdaThread *thread = new LambdaThread([this, debugging, script, context, editor]() {
    PythonContext::AddDebuggableThread();

    scriptContext = context;
    runningScriptEditor = editor;
    context->executeString(lit("script.py"), script, debugging);
    scriptContext = NULL;

    GUIInvoke::call(this, [this, context]() {
      m_CurLineTimer->stop();

      context->Finish();
      runningScriptEditor = NULL;
      enableButtons(true);
    });

    PythonContext::RemoveDebuggableThread();
  });

  thread->setName(lit("Python script"));
  thread->selfDelete(true);
  thread->start();

  if(debugging)
    PythonContext::LaunchDebugger(this, m_Ctx.Config(), QString());
}

void PythonShell::on_findReplace_clicked()
{
  m_FindReplace->raiseOrShow();
}

void PythonShell::on_execute_clicked()
{
  QString command = ui->lineInput->text();

  ANALYTIC_SET(UIFeatures.PythonInterop, true);

  appendText(ui->interactiveOutput, command + lit("\n"));

  if(!command.trimmed().isEmpty())
    history.push_front(command);
  historyidx = -1;

  ui->lineInput->clear();

  // assume a trailing colon means there will be continuation. Store the command and add a continue
  // prompt. If we're already continuing, then wait until we get a blank line before executing.
  if(command.trimmed().right(1) == lit(":") || (!m_storedLines.isEmpty() && !command.isEmpty()))
  {
    appendText(ui->interactiveOutput, lit(".. "));
    m_storedLines += command + lit("\n");
    return;
  }

  // concatenate any previous lines if we are doing a multi-line command.
  command = m_storedLines + command;
  m_storedLines = QString();

  if(command.trimmed().length() > 0)
  {
    interactiveContext->executeString(command);
    interactiveContext->reflectSource(QString());
  }

  appendText(ui->interactiveOutput, lit(">> "));
}

void PythonShell::on_clear_clicked()
{
  QString minidocHeader = scriptHeader();

  minidocHeader += lit("\n\n>> ");

  ui->interactiveOutput->setText(minidocHeader);

  if(interactiveContext)
    interactiveContext->Finish();

  interactiveContext = newContext();
  interactiveContext->reflectSource(QString());
}

void PythonShell::on_newScript_clicked()
{
  QString minidocHeader = scriptHeader();

  minidocHeader.replace(QLatin1Char('\n'), lit("\n# "));

  minidocHeader = QFormatStr("# %1\n\n").arg(minidocHeader);

  ScintillaEdit *editor = makeEditor("");

  ui->saveScript->setEnabled(false);

  editor->setText(minidocHeader.toUtf8().data());
  editor->emptyUndoBuffer();
  markEditorModified(editor, false);
}

void PythonShell::on_openScript_clicked()
{
  QString filename = RDDialog::getOpenFileName(this, tr("Open Python Script"), QString(),
                                               tr("Python scripts (*.py)"));

  if(!LoadScriptFromFilename(filename))
  {
    RDDialog::critical(this, tr("Error loading script"), tr("Couldn't open path %1.").arg(filename));
  }
}

void PythonShell::on_saveScript_clicked()
{
  ScintillaEdit *editor = curEditor();

  if(!editor)
    return;

  QString filename = getEditorFilename(editor);

  if(!QFileInfo(filename).isAbsolute())
    return on_saveAsScript_clicked();

  if(saveEditor(editor, filename))
    markEditorModified(editor, false);
}

void PythonShell::on_saveAsScript_clicked()
{
  ScintillaEdit *editor = curEditor();

  if(!editor)
    return;

  if(saveEditorAs(editor))
    markEditorModified(editor, false);
}

void PythonShell::on_runScript_clicked()
{
  RunScript();
}

void PythonShell::on_debugScript_clicked()
{
  DebugScript();
}

void PythonShell::on_abortRun_clicked()
{
  if(scriptContext)
    scriptContext->abort();
}

void PythonShell::traceLine(const QString &file, int line)
{
  if(!runningScriptEditor)
    return;

  // we only update the current line on a fixed timer to avoid DoS'ing ourselves with too many rapid
  // updates. Both happen on the UI thread so we just need a simple flag
  m_CurLine = line;
  m_CurLineDirty = true;
}

void PythonShell::exception(const QString &extension, const QString &type, const QString &value,
                            int finalLine, QList<QString> frames)
{
  if(finalLine >= 0)
    traceLine(QString(), finalLine);

  QString exString;

  if(!frames.isEmpty())
  {
    exString += tr("Traceback (most recent call last):\n");
    for(const QString &f : frames)
      exString += QFormatStr("  %1\n").arg(f);
  }
  exString += QFormatStr("%1: %2\n").arg(type).arg(value);

  if(QObject::sender() == (QObject *)interactiveContext)
  {
    appendText(ui->interactiveOutput, exString);
    return;
  }

  exString.insert(0, QLatin1Char('\n'));

  scriptOutputLines.push_back({extension, exString});

  updateScriptOutput(false);
}

void PythonShell::textOutput(const QString &extension, bool isStdError, const QString &output)
{
  if(QObject::sender() == (QObject *)interactiveContext)
  {
    appendText(ui->interactiveOutput, output);
    return;
  }

  scriptOutputLines.push_back({extension, output});
  updateScriptOutput(false);
}

void PythonShell::on_outputContext_currentIndexChanged(int idx)
{
  updateScriptOutput(true);
}

void PythonShell::on_projectExplorer_itemActivated(RDTreeWidgetItem *item, int column)
{
  // ignore these, just let them collapse/expand
  if(item == m_Examples || item == m_UIExtensions || item == m_RecentFiles)
    return;

  if(item->parent() == m_Examples)
  {
    QString filename = tr("Example: ") + item->text(0);
    QString text = item->data(0, Qt::UserRole).toString();

    for(ScintillaEdit *edit : m_Scintillas)
    {
      if(getEditorFilename(edit) == filename)
      {
        ToolWindowManager::raiseToolWindow(edit);
        return;
      }
    }

    CreateNewScriptEditor(filename, text);
  }
  else
  {
    // recent file or UI extension, the user role contains the path
    QString filename = item->data(0, Qt::UserRole).toString();

    // directories in UI extensions have no filename to activate
    if(filename.isEmpty())
      return;

    for(ScintillaEdit *edit : m_Scintillas)
    {
      if(getEditorFilename(edit) == filename)
      {
        ToolWindowManager::raiseToolWindow(edit);
        return;
      }
    }

    if(!QFileInfo(filename).exists())
    {
      QMessageBox::StandardButton response = RDDialog::question(
          this, tr("'%1' does not exist").arg(QFileInfo(filename).fileName()),
          tr("File not found at path:\n%1\n\nRemove from recent files list?").arg(filename));

      if(response == QMessageBox::Yes)
      {
        rdcstr f = filename;
        m_Ctx.Config().Python_RecentFiles.removeIf([f](const rdcstr &o) { return o == f; });

        m_Ctx.Config().Save();

        GUIInvoke::defer(this, [this]() { updateRecentFiles(false); });
      }

      return;
    }

    LoadScriptFromFilename(filename);

    QString unsavedFile = interactiveContext->GetTempFilename(lit("script.py"));
    if(filename == unsavedFile)
    {
      QFile::remove(unsavedFile);
      updateRecentFiles(false);
    }
  }
}

bool PythonShell::checkAllowClose()
{
  for(ScintillaEdit *edit : m_Scintillas)
  {
    EditorWrapper *wrap = (EditorWrapper *)edit;

    if(!wrap->checkAllowClose())
      return false;
  }
  return true;
}

void PythonShell::updateScriptOutput(bool fullRefresh)
{
  if(fullRefresh)
  {
    ui->scriptOutput->clear();
    lastDisplayedLine = 0;
  }

  for(size_t i = lastDisplayedLine; i < scriptOutputLines.size(); i++)
  {
    bool display = false;

    if(ui->outputContext->currentIndex() == AllOutputFilter)
      display = true;

    // Script
    else if(ui->outputContext->currentIndex() == ScriptOutputFilter)
      display = scriptOutputLines[i].extension.isEmpty();

    else if(loadedExtensions.indexOf(scriptOutputLines[i].extension) + FirstExtensionOutputFilter ==
            ui->outputContext->currentIndex())
      display = true;

    if(display)
    {
      appendText(ui->scriptOutput, scriptOutputLines[i].text);
    }
  }

  lastDisplayedLine = scriptOutputLines.size();
}

bool PythonShell::saveEditorAs(ScintillaEdit *editor)
{
  QString filename = RDDialog::getSaveFileName(this, tr("Save Python Script"), QString(),
                                               tr("Python scripts (*.py)"));
  if(filename.isEmpty())
    return false;
  return saveEditor(editor, filename);
}

bool PythonShell::saveEditor(ScintillaEdit *editor, QString filename)
{
  QString oldFilename = getEditorFilename(editor);
  if(!filename.isEmpty())
  {
    QDir dirinfo = QFileInfo(filename).dir();
    if(dirinfo.exists())
    {
      QFile f(filename);
      if(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      {
        // remove the path from watching first so we don't get a notification from the write itself
        m_Watcher->removePath(oldFilename);

        QString text = QString::fromUtf8(editor->getText(editor->textLength() + 1));
        text.remove(QLatin1Char('\r'));
        f.write(text.toUtf8());

        addRecentFile(filename);

        // delay a short while before starting to watch this file. This is highly unlikely to miss
        // any real writes (which would have to happen externally after we save), but prevents us
        // from identifying our own writes as an external modification.
        QTimer::singleShot(200, [this, filename]() { m_Watcher->addPath(filename); });

        setEditorFilename(editor, filename);
        return true;
      }
      else
      {
        RDDialog::critical(
            this, tr("Error saving script"),
            tr("Couldn't open path %1 for write.\n%2").arg(filename).arg(f.errorString()));
      }
    }
    else
    {
      RDDialog::critical(this, tr("Invalid directory"),
                         tr("Cannot find target directory to save to:\n%1").arg(filename));
    }
  }
  return false;
}

void PythonShell::extensionLoaded(const QString &extension)
{
  ui->outputContext->addItem(tr("Extension %1").arg(extension));
  loadedExtensions.push_back(extension);
}

void PythonShell::editor_contextMenu(const QPoint &pos)
{
  ScintillaEdit *editor = qobject_cast<ScintillaEdit *>(QObject::sender());

  if(!editor)
    return;

  hideFunccompleteTooltip();

  m_ContextMenuVisible = true;

  QMenu contextMenu(this);

  QString typeName;

  sptr_t scintillaPos = editor->positionFromPoint(pos.x(), pos.y());
  if(scintillaPos >= 0)
  {
    sptr_t line = editor->lineFromPosition(scintillaPos);
    sptr_t col = scintillaPos - editor->positionFromLine(line);

    typeName = completionContext->typenameForLoc(line + 1, col);
  }

  bool valid = !typeName.isEmpty();

  QAction help(valid ? tr("Help for '%1'").arg(typeName) : tr("Help"), this);

  QObject::connect(&help, &QAction::triggered, [this, typeName] { selectedHelp(typeName); });

  help.setEnabled(valid);

  contextMenu.addAction(&help);
  contextMenu.addSeparator();

  QAction undo(tr("Undo"), this);
  QAction redo(tr("Redo"), this);

  QObject::connect(&undo, &QAction::triggered, [this, editor] { editor->undo(); });
  QObject::connect(&redo, &QAction::triggered, [this, editor] { editor->redo(); });

  undo.setEnabled(editor->canUndo());
  redo.setEnabled(editor->canRedo());

  contextMenu.addAction(&undo);
  contextMenu.addAction(&redo);
  contextMenu.addSeparator();

  QAction cutText(tr("Cut"), this);
  QAction copyText(tr("Copy"), this);
  QAction pasteText(tr("Paste"), this);
  QAction deleteText(tr("Delete"), this);

  QObject::connect(&cutText, &QAction::triggered, [this, editor] { editor->cut(); });

  QObject::connect(&copyText, &QAction::triggered, [this, editor] {
    editor->copyRange(editor->selectionStart(), editor->selectionEnd());
  });

  QObject::connect(&pasteText, &QAction::triggered, [this, editor] { editor->paste(); });

  QObject::connect(&deleteText, &QAction::triggered, [this, editor] {
    editor->deleteRange(editor->selectionStart(), editor->selectionEnd());
  });

  contextMenu.addAction(&cutText);
  contextMenu.addAction(&copyText);
  contextMenu.addAction(&pasteText);
  contextMenu.addAction(&deleteText);
  contextMenu.addSeparator();

  if(editor->selectionEmpty())
  {
    cutText.setEnabled(false);
    copyText.setEnabled(false);
    deleteText.setEnabled(false);
  }

  pasteText.setEnabled(editor->canPaste());

  QAction selectAll(tr("Select All"), this);
  QObject::connect(&selectAll, &QAction::triggered, [this, editor] { editor->selectAll(); });
  contextMenu.addAction(&selectAll);

  RDDialog::show(&contextMenu, editor->viewport()->mapToGlobal(pos));

  m_ContextMenuVisible = false;
}

void PythonShell::selectedHelp(QString word)
{
  ui->helpSearch->setText(word);

  refreshCurrentHelp();
}

void PythonShell::refreshCurrentHelp()
{
  ToolWindowManager::raiseToolWindow(ui->helpGroup);

  ui->helpText->clear();

  m_HelpPrinting = true;

  completionContext->executeString(lit(R"(
try:
  import keyword
  if keyword.iskeyword("%1"):
    help("%1")
  else:
    help(%1)
except ImportError:
  help(%1)
)")
                                       .arg(ui->helpSearch->text()));

  ui->helpText->verticalScrollBar()->setValue(0);

  m_HelpPrinting = false;
}

void PythonShell::interactive_keypress(QKeyEvent *event)
{
  bool triggerCompletion = false;

  if(m_InteractiveCompleter->popup()->isVisible())
  {
    switch(event->key())
    {
      // manually trigger a completion with tab
      case Qt::Key_Tab:
        m_InteractiveCompleter->activated(
            m_InteractiveCompleter->popup()->selectionModel()->currentIndex());
        m_InteractiveCompleter->popup()->hide();
        return;
      // if a completion is in progress ignore any events the completer will process
      case Qt::Key_Return:
      case Qt::Key_Enter: return;
      // allow key scrolling
      case Qt::Key_Up:
      case Qt::Key_Down:
      case Qt::Key_PageUp:
      case Qt::Key_PageDown:
        break;
        // all other keys close the popup
      default: triggerCompletion = true;
    }
  }
  else
  {
    if(event->text() != QString() && event->text()[0].isPrint() && event->key() != Qt::Key_Return &&
       event->key() != Qt::Key_Enter)
      triggerCompletion = true;

    if(event->key() == Qt::Key_Escape && m_FuncTip && m_ToolTip->isVisible())
      hideFunccompleteTooltip();
  }

  if(triggerCompletion)
  {
    QString base = ui->lineInput->text();

    QStringList completions;

    if(base.trimmed() != QString())
      completions = interactiveContext->completionOptions(0, base, m_InteractiveCompletionPrefix);

    if(completions.isEmpty())
    {
      if(event->key() == Qt::Key_Tab)
        ui->lineInput->insert(lit("\t"));
      m_InteractiveCompleter->popup()->hide();

      QString prompt = interactiveContext->tryFunctionCompletion(0, base);

      if(!prompt.isEmpty())
      {
        m_ToolTip->configureTip(this, prompt);

        QPoint p = ui->lineInput->fontMetrics().boundingRect(base).bottomRight();
        p.setY(ui->lineInput->geometry().height());
        p = ui->lineInput->mapToGlobal(p);
        if(!m_ToolTip->isVisible())
          m_ToolTip->showTipAtPos(p);
        m_FuncTip = true;
        m_FuncTipWidget = ui->lineInput;
      }
      else
      {
        hideFunccompleteTooltip();
      }

      return;
    }

    hideFunccompleteTooltip();

    m_InteractiveCompletionModel->setStringList(completions);

    QRect r = ui->lineInput->rect();
    QFontMetrics fm = ui->lineInput->fontMetrics();

#if(QT_VERSION < QT_VERSION_CHECK(5, 11, 0))
#define horizontalAdvance width
#endif

    int longestWidth = 0;
    for(QString &c : completions)
    {
      longestWidth = qMax(longestWidth, fm.horizontalAdvance(c));
    }

    base.resize(base.size() - m_InteractiveCompletionPrefix);

    r.setLeft(r.left() + fm.horizontalAdvance(base));
    r.setWidth(longestWidth + ui->lineInput->style()->pixelMetric(QStyle::PM_ScrollBarExtent) +
               ui->lineInput->style()->pixelMetric(QStyle::PM_ButtonMargin));

    m_InteractiveCompleter->complete(r);

    return;
  }

  if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
  {
    on_execute_clicked();
  }

  bool moved = false;

  if(event->key() == Qt::Key_Down && historyidx > -1)
  {
    historyidx--;

    moved = true;
  }

  QString workingtext;

  if(event->key() == Qt::Key_Up && historyidx + 1 < history.count())
  {
    if(historyidx == -1)
      workingtext = ui->lineInput->text();

    historyidx++;

    moved = true;
  }

  if(moved)
  {
    if(historyidx == -1)
      ui->lineInput->setText(workingtext);
    else
      ui->lineInput->setText(history[historyidx]);

    ui->lineInput->deselect();
  }
}

void PythonShell::helpSearch_keypress(QKeyEvent *e)
{
  if(e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
    refreshCurrentHelp();
}

QString PythonShell::scriptHeader()
{
  return tr(R"(RenderDoc Python console, powered by python %1.
The 'pyrenderdoc' object is the current CaptureContext instance.
The 'renderdoc' and 'qrenderdoc' modules are available.
Documentation is available: https://renderdoc.org/docs/python_api/index.html)")
      .arg(interactiveContext->versionString());
}

void PythonShell::appendText(QTextEdit *output, const QString &text)
{
  output->moveCursor(QTextCursor::End);
  output->insertPlainText(text);

  // scroll to the bottom
  QScrollBar *vscroll = output->verticalScrollBar();
  vscroll->setValue(vscroll->maximum());
}

void PythonShell::enableButtons(bool enable)
{
  ui->newScript->setEnabled(enable);
  ui->openScript->setEnabled(enable);
  ui->saveScript->setEnabled(enable);
  ui->runScript->setEnabled(enable);
  ui->abortRun->setEnabled(!enable);
  ui->debugScript->setEnabled(enable);

  if(enable && !m_Ctx.Config().Python_DebugEnabled)
  {
    ui->debugScript->setEnabled(false);
  }
}

void PythonShell::doAutocomplete(ScintillaEdit *editor)
{
  sptr_t pos = editor->currentPos();
  sptr_t line = editor->lineFromPosition(pos);
  sptr_t lineStart = editor->positionFromLine(line);
  QByteArray lineText = editor->getLine(line);
  lineText.resize(pos - lineStart);

  int prefix_len = 0;
  QStringList completions =
      completionContext->completionOptions(line, QString::fromUtf8(lineText), prefix_len);

  if(completions.empty())
  {
    doFunccomplete(editor);
    return;
  }

  hideFunccompleteTooltip();
  editor->autoCShow(prefix_len, completions.join(QLatin1Char(' ')).toUtf8().data());
}

void PythonShell::doFunccomplete(ScintillaEdit *editor)
{
  sptr_t pos = editor->currentPos();
  sptr_t line = editor->lineFromPosition(pos);
  sptr_t lineStart = editor->positionFromLine(line);
  QByteArray lineText = editor->getLine(line);
  lineText.resize(pos - lineStart);

  QString prompt = completionContext->tryFunctionCompletion(line, QString::fromUtf8(lineText));

  if(!prompt.isEmpty())
  {
    m_ToolTip->configureTip(this, prompt);

    sptr_t tooltipPos = editor->positionFromLine(line + 1);

    QPoint p(editor->pointXFromPosition(tooltipPos),
             editor->pointYFromPosition(lineStart + lineText.size()) + editor->textHeight(line));
    p = editor->mapToGlobal(p);
    if(!m_ToolTip->isVisible())
      m_ToolTip->showTipAtPos(p);
    m_FuncTip = true;
    m_FuncTipWidget = editor;
    m_FuncTipLine = line;
  }
  else
  {
    hideFunccompleteTooltip();
  }
}

void PythonShell::hideFunccompleteTooltip()
{
  m_ToolTip->hideTip();
  m_FuncTip = false;
  m_FuncTipWidget = NULL;
  // start the syntax check timer in case this naturally disappeared
  m_SyntaxCheckTimer->start();
}

PythonContext *PythonShell::newContext()
{
  PythonContext *ret = new PythonContext();

  QObject::connect(ret, &PythonContext::traceLine, this, &PythonShell::traceLine);
  QObject::connect(ret, &PythonContext::exception, this, &PythonShell::exception);
  QObject::connect(ret, &PythonContext::textOutput, this, &PythonShell::textOutput);

  setGlobals(ret);

  return ret;
}

void PythonShell::setGlobals(PythonContext *ret)
{
  ret->setGlobal("pyrenderdoc", m_ThreadCtx);
}
