/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2018-2026 Baldur Karlsson
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

#include "ExtensionManager.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QKeyEvent>
#include <QRegularExpression>
#include "Code/Interface/QRDInterface.h"
#include "Code/Resources.h"
#include "Code/pyrenderdoc/PythonContext.h"
#include "Widgets/Extended/RDHeaderView.h"
#include "Widgets/Extended/RDLabel.h"
#include "Widgets/Extended/RDLineEdit.h"
#include "Windows/MainWindow.h"
#include "ui_ExtensionManager.h"

Q_DECLARE_METATYPE(ExtensionMetadata);

ExtensionManager::ExtensionManager(ICaptureContext &ctx)
    : QDialog(NULL), ui(new Ui::ExtensionManager), m_Ctx(ctx)
{
  ui->setupUi(this);
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->extensions->setHeader(header);

    ui->extensions->setColumns({tr("Package"), tr("Name"), tr("Loaded")});
    header->setColumnStretchHints({1, 4, -1});
  }

  ui->name->setText(lit("---"));
  ui->version->setText(lit("---"));
  ui->author->setText(lit("---"));
  ui->URL->setText(lit("---"));
  ui->reload->setEnabled(false);
  ui->debug->setEnabled(false);
  ui->alwaysLoad->setEnabled(false);

  QObject::connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

  PopulateExtensionList();
}

void ExtensionManager::PopulateExtensionList()
{
  ui->extensions->clear();

  QString extensionFolder = ConfigFilePath("extensions");

  m_Extensions = m_Ctx.Extensions().GetInstalledExtensions();

  if(m_Extensions.isEmpty())
  {
    QString contrib_url = lit("https://github.com/baldurk/renderdoc-contrib");
    ui->extensions->addTopLevelItem(
        new RDTreeWidgetItem({QString(), tr("No extensions found available"), QString()}));
    ui->extensions->addTopLevelItem(new RDTreeWidgetItem(
        {QString(), tr("Create packages in %1").arg(extensionFolder), QString()}));
    ui->extensions->addTopLevelItem(new RDTreeWidgetItem(
        {QString(), tr("Browse extensions at %1").arg(contrib_url), QString()}));
    ui->URL->setText(lit("<a href=\"%1\">%1</a>").arg(contrib_url));
  }
  else
  {
    for(const ExtensionMetadata &e : m_Extensions)
    {
      RDTreeWidgetItem *item = new RDTreeWidgetItem({e.package, e.name, QString()});

      item->setCheckState(
          2, m_Ctx.Extensions().IsExtensionLoaded(e.package) ? Qt::Checked : Qt::Unchecked);

      ui->extensions->addTopLevelItem(item);
    }

    ui->extensions->setCurrentItem(ui->extensions->topLevelItem(0));
  }
}

ExtensionManager::~ExtensionManager()
{
  delete ui;
}

void ExtensionManager::on_reload_clicked()
{
  RDTreeWidgetItem *item = ui->extensions->currentItem();
  if(!item)
    return;

  int idx = ui->extensions->indexOfTopLevelItem(item);

  if(idx >= 0 && idx < m_Extensions.count())
  {
    const ExtensionMetadata &e = m_Extensions[idx];
    if(!e.name.isEmpty())
    {
      // if the load succeeds, set us as checked. Otherwise, unchecked
      QString errors = m_Ctx.Extensions().LoadExtension(e.package);
      if(errors.isEmpty())
      {
        item->setCheckState(2, Qt::Checked);
      }
      else
      {
        item->setCheckState(2, Qt::Unchecked);
        RDDialog::critical(this, tr("Failed to load extension"),
                           tr("Failed to load extension '%1':\n"
                              "%2")
                               .arg(e.name)
                               .arg(errors));
      }

      update_currentItem(item);
    }
  }
}

void ExtensionManager::on_debug_clicked()
{
  if(m_Extensions.empty())
    return;

  RDTreeWidgetItem *item = ui->extensions->currentItem();
  if(!item)
    return;

  int idx = ui->extensions->indexOfTopLevelItem(item);

  if(idx >= 0 && idx < m_Extensions.count())
  {
    const ExtensionMetadata &e = m_Extensions[idx];
    if(!e.name.isEmpty())
    {
      PythonContext::PrepareDebuggerWait();

      LambdaThread *thread = new LambdaThread([this]() {
        PythonContext::WaitForDebugger();

        GUIInvoke::call(this, [this]() { on_reload_clicked(); });
      });

      thread->selfDelete(true);
      thread->start();

      PythonContext::LaunchDebugger(this, m_Ctx.Config(), QFileInfo(e.filePath).absoluteFilePath());
    }
  }
}

void ExtensionManager::on_output_clicked()
{
  m_Ctx.ShowPythonShell();
  m_Ctx.GetPythonShell()->ShowOutput();

  RDTreeWidgetItem *item = ui->extensions->currentItem();
  if(item)
  {
    int idx = ui->extensions->indexOfTopLevelItem(item);

    if(idx >= 0 && idx < m_Extensions.count())
    {
      const ExtensionMetadata &e = m_Extensions[idx];
      if(!e.package.isEmpty())
      {
        m_Ctx.GetPythonShell()->SetExtensionOutputFilter(e.package);
      }
    }
  }

  accept();
}

void ExtensionManager::on_openLocation_clicked()
{
  if(m_Extensions.empty())
  {
    QDesktopServices::openUrl(QString(ConfigFilePath("extensions")));
    return;
  }

  RDTreeWidgetItem *item = ui->extensions->currentItem();
  if(!item)
    return;

  int idx = ui->extensions->indexOfTopLevelItem(item);

  if(idx >= 0 && idx < m_Extensions.count())
  {
    const ExtensionMetadata &e = m_Extensions[idx];
    if(!e.name.isEmpty())
    {
      QDesktopServices::openUrl(QFileInfo(e.filePath).absoluteFilePath());
    }
  }
}

void ExtensionManager::on_alwaysLoad_toggled(bool checked)
{
  RDTreeWidgetItem *item = ui->extensions->currentItem();
  if(!item)
    return;

  int idx = ui->extensions->indexOfTopLevelItem(item);

  if(idx >= 0 && idx < m_Extensions.count())
  {
    const ExtensionMetadata &e = m_Extensions[idx];
    if(!e.name.isEmpty())
    {
      m_Ctx.Config().AlwaysLoad_Extensions.removeOne(e.package);
      if(checked)
        m_Ctx.Config().AlwaysLoad_Extensions.push_back(e.package);

      m_Ctx.Config().Save();
    }
  }
}

void ExtensionManager::on_createExtension_clicked()
{
  QDialog dialog;
  RDLabel label;
  RDLineEdit extensionName;
  QDialogButtonBox buttons;

  dialog.setWindowTitle(tr("Create new UI extension"));
  dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);

  label.setText(
      tr("Create a new UI extension, with some example code.\n"
         "\n"
         "This will create the directory structure for the specified package name, with a default\n"
         "extension metadata json and some simple example code to give you a starting point."));

  extensionName.setPlaceholderText(tr("myname.example"));

  buttons.setOrientation(Qt::Horizontal);
  buttons.setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons.setCenterButtons(true);

  QObject::connect(&buttons, &QDialogButtonBox::accepted, [this, &dialog, &extensionName]() {
    QString extName = extensionName.text().trimmed();

    if(extName.isEmpty())
    {
      RDDialog::critical(&dialog, tr("Invalid extension name"),
                         tr("Must specify a name for the new extension."));
      return;
    }

    if(extName.startsWith(lit("renderdoc.")))
    {
      RDDialog::critical(&dialog, tr("Invalid extension name"),
                         tr("Extension name conflicts with builtin module 'renderdoc'."));
      return;
    }

    if(extName.contains(QLatin1Char(' ')) || extName.contains(QLatin1Char('\t')))
    {
      RDDialog::critical(
          &dialog, tr("Invalid extension name"),
          tr("Extension names should be valid python package names, note including whitespace."));
      return;
    }

    for(const ExtensionMetadata &e : m_Extensions)
    {
      if(QString(e.package) == extName)
      {
        RDDialog::critical(&dialog, tr("Extension name in use"),
                           tr("The extension name '%1' already exists.").arg(e.package));
        return;
      }
    }

    QStringList locations = PythonContext::GetApplicationExtensionsPaths();

    if(!locations.empty())
    {
      QDir dir(locations[0]);

      QStringList paths = extName.split(QLatin1Char('.'));

      bool nonexist = false;

      while(!paths.empty())
      {
        QString dirname = paths[0];
        paths.pop_front();

        if(!dir.cd(dirname))
        {
          nonexist = true;
          break;
        }

        qInfo() << dir.absolutePath();
      }

      if(!nonexist && dir.exists() && !dir.isEmpty())
      {
        RDDialog::critical(&dialog, tr("Directory already exists"),
                           tr("Extension directory already exists:\n%1").arg(dir.absolutePath()));
        return;
      }
    }

    dialog.accept();
  });
  QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  QVBoxLayout *layout = new QVBoxLayout(&dialog);
  layout->addWidget(&label);
  layout->addWidget(&extensionName);
  layout->addWidget(&buttons);

  if(!RDDialog::show(&dialog))
    return;

  if(dialog.result() == QDialog::Accepted)
  {
    QStringList locations = PythonContext::GetApplicationExtensionsPaths();
    QDir dir(locations[0]);

    QString extName = extensionName.text().trimmed();
    QStringList paths = extName.split(QLatin1Char('.'));

    while(!paths.empty())
    {
      QString dirname = paths[0];
      paths.pop_front();

      dir.mkdir(dirname);

      if(!dir.cd(dirname))
      {
        RDDialog::critical(&dialog, tr("Couldn't create directory"),
                           tr("Failed to create %1 in %2").arg(dirname).arg(dir.absolutePath()));
        return;
      }
    }

    paths = extName.split(QLatin1Char('.'));

    QString metadata = lit(R"({
	"extension_api": 1,
	"name": "%3",
	"version": "1.0",
	"minimum_renderdoc": "%1.%2",
	"description": "Template extension %4",
	"author": "My Name <my.email@example.com>",
	"url": "https://github.com/example/example"
}
)")
                           .arg(RENDERDOC_VERSION_MAJOR)
                           .arg(RENDERDOC_VERSION_MINOR)
                           .arg(paths.back())
                           .arg(extName);

    {
      QFile ext(dir.absoluteFilePath(lit("extension.json")));
      if(ext.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      {
        ext.write(metadata.toUtf8());
      }

      QFile init(dir.absoluteFilePath(lit("__init__.py")));
      if(init.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      {
        init.write(R"(
# Blank RenderDoc UI extension

import renderdoc as rd
import qrenderdoc as qrd

def register(version: str, pyrenderdoc: qrd.CaptureContext):
    print(f"New UI extension loaded in RenderDoc {version}")

def unregister():
    print(f"New UI extension being unloaded")
)");
      }
    }

    PopulateExtensionList();
  }
}

void ExtensionManager::on_extensions_currentItemChanged(RDTreeWidgetItem *item, RDTreeWidgetItem *)
{
  update_currentItem(item);
}

void ExtensionManager::on_extensions_itemChanged(RDTreeWidgetItem *item, int col)
{
  if(col == 2)
  {
    ui->extensions->setCurrentItem(item);

    bool loaded = m_Ctx.Extensions().IsExtensionLoaded(item->text(0));

    // if the extension is loaded, don't allow unchecking
    if(loaded && item->checkState(2) != Qt::Checked)
    {
      item->setCheckState(2, Qt::Checked);
      return;
    }

    // if the extension is unloaded, if we're now checked then try to load it. If
    // we're unchecked allow that (it is a code-change after we failed to load)
    if(!loaded)
    {
      if(item->checkState(2) == Qt::Checked)
        on_reload_clicked();
    }
  }
}

void ExtensionManager::update_currentItem(RDTreeWidgetItem *item)
{
  if(!item)
    return;

  if(item != ui->extensions->currentItem())
  {
    ui->extensions->setCurrentItem(item);
    return;
  }

  int idx = ui->extensions->indexOfTopLevelItem(item);

  if(idx >= 0 && idx < m_Extensions.count())
  {
    const ExtensionMetadata &e = m_Extensions[idx];
    if(!e.name.isEmpty())
    {
      QRegularExpression authRE(lit("^(.*) <(.*)>$"));

      ui->name->setText(e.name);
      ui->version->setText(e.version);
      ui->URL->setText(QFormatStr("<a href=\"%1\">%1</a>").arg(e.extensionURL));
      ui->description->setText(e.description);

      QRegularExpressionMatch match = authRE.match(QString(e.author).trimmed());

      if(match.hasMatch() && match.captured(2).contains(QLatin1Char('@')))
        ui->author->setText(
            QFormatStr("<a href=\"mailto:%2\">%1</a>").arg(match.captured(1)).arg(match.captured(2)));
      else
        ui->author->setText(e.author);

      bool loaded = item->checkState(2) == Qt::Checked;
      ui->reload->setEnabled(true);
      ui->reload->setText(loaded ? tr("Reload") : tr("Load"));
      ui->output->setEnabled(loaded);
      ui->debug->setEnabled(loaded && PythonContext::IsDebuggingEnabled());
      ui->debug->setToolTip(QString());
      if(loaded && !PythonContext::IsDebuggingEnabled())
        ui->debug->setToolTip(
            tr("Debugging not supported - check documentation for setup instructions"));
      ui->alwaysLoad->setEnabled(loaded);

      ui->alwaysLoad->setChecked(m_Ctx.Config().AlwaysLoad_Extensions.contains(e.package));
    }
  }
}
