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
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QStringListModel>
#include <QTimer>
#include "Code/QRDUtils.h"
#include "Code/ScintillaSyntax.h"
#include "Code/pyrenderdoc/PythonContext.h"
#include "Widgets/Extended/RDToolTip.h"
#include "scintilla/include/SciLexer.h"
#include "toolwindowmanager/ToolWindowManagerArea.h"
#include "ui_PythonShell.h"

enum
{
  AllOutputFilter,
  ScriptOutputFilter,
  FirstExtensionOutputFilter,
};

// a forwarder that invokes onto the UI thread wherever necessary.
// Note this does NOT make CaptureContext thread safe. We just invoke for any potentially UI
// operations. All invokes are blocking, so there can't be any times when the UI thread waits
// on the python thread.
template <typename Obj>
struct ObjectForwarder : Obj
{
  ObjectForwarder(PythonShell *sh, Obj &o) : m_Shell(sh), m_Obj(o) {}
  PythonShell *m_Shell;
  Obj &m_Obj;

  template <typename F, typename... paramTypes>
  void InvokeVoidFunction(F ptr, paramTypes... params)
  {
    if(!GUIInvoke::onUIThread())
    {
      PythonContext *scriptContext = m_Shell->GetScriptContext();
      if(scriptContext)
        scriptContext->PausePythonThreading();
      GUIInvoke::blockcall(m_Shell, [this, ptr, params...]() { (m_Obj.*ptr)(params...); });
      if(scriptContext)
        scriptContext->ResumePythonThreading();
      return;
    }

    (m_Obj.*ptr)(params...);
  }

  template <typename R, typename F, typename... paramTypes>
  R InvokeRetFunction(F ptr, paramTypes... params)
  {
    if(!GUIInvoke::onUIThread())
    {
      R ret;
      PythonContext *scriptContext = m_Shell->GetScriptContext();
      if(scriptContext)
        scriptContext->PausePythonThreading();
      GUIInvoke::blockcall(m_Shell,
                           [this, &ret, ptr, params...]() { ret = (m_Obj.*ptr)(params...); });
      if(scriptContext)
        scriptContext->ResumePythonThreading();
      return ret;
    }

    return (m_Obj.*ptr)(params...);
  }
};

struct MiniQtInvoker : ObjectForwarder<IMiniQtHelper>
{
  MiniQtInvoker(PythonShell *shell, IMiniQtHelper &obj) : ObjectForwarder(shell, obj) {}
  virtual ~MiniQtInvoker() {}
  void InvokeOntoUIThread(std::function<void()> callback)
  {
    // this function is already thread safe since it's invoking, so just call it directly
    m_Obj.InvokeOntoUIThread(callback);
  }

  ///////////////////////////////////////////////////////////////////////
  // all functions invoke onto the UI thread since they deal with widgets!
  ///////////////////////////////////////////////////////////////////////

  QWidget *CreateToplevelWidget(const rdcstr &windowTitle, WidgetCallback closed)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateToplevelWidget, windowTitle, closed);
  }
  void CloseToplevelWidget(QWidget *widget)
  {
    InvokeVoidFunction(&IMiniQtHelper::CloseToplevelWidget, widget);
  }

  // widget hierarchy

  void SetWidgetName(QWidget *widget, const rdcstr &name)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetWidgetName, widget, name);
  }
  rdcstr GetWidgetName(QWidget *widget)
  {
    return InvokeRetFunction<rdcstr>(&IMiniQtHelper::GetWidgetName, widget);
  }
  rdcstr GetWidgetType(QWidget *widget)
  {
    return InvokeRetFunction<rdcstr>(&IMiniQtHelper::GetWidgetType, widget);
  }
  QWidget *FindChildByName(QWidget *parent, const rdcstr &name)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::FindChildByName, parent, name);
  }
  QWidget *GetParent(QWidget *widget)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::GetParent, widget);
  }
  int32_t GetNumChildren(QWidget *widget)
  {
    return InvokeRetFunction<int32_t>(&IMiniQtHelper::GetNumChildren, widget);
  }
  QWidget *GetChild(QWidget *parent, int32_t index)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::GetChild, parent, index);
  }
  void DestroyWidget(QWidget *widget) { InvokeVoidFunction(&IMiniQtHelper::DestroyWidget, widget); }
  // dialogs

  bool ShowWidgetAsDialog(QWidget *widget)
  {
    return InvokeRetFunction<bool>(&IMiniQtHelper::ShowWidgetAsDialog, widget);
  }
  void CloseCurrentDialog(bool success)
  {
    InvokeVoidFunction(&IMiniQtHelper::CloseCurrentDialog, success);
  }

  // layout functions

  QWidget *CreateHorizontalContainer()
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateHorizontalContainer);
  }
  QWidget *CreateVerticalContainer()
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateVerticalContainer);
  }
  QWidget *CreateGridContainer()
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateGridContainer);
  }
  QWidget *CreateSpacer(bool horizontal)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateSpacer, horizontal);
  }
  void ClearContainedWidgets(QWidget *parent)
  {
    InvokeVoidFunction(&IMiniQtHelper::ClearContainedWidgets, parent);
  }
  void AddGridWidget(QWidget *parent, int32_t row, int32_t column, QWidget *child, int32_t rowSpan,
                     int32_t columnSpan)
  {
    InvokeVoidFunction(&IMiniQtHelper::AddGridWidget, parent, row, column, child, rowSpan,
                       columnSpan);
  }
  void AddWidget(QWidget *parent, QWidget *child)
  {
    InvokeVoidFunction(&IMiniQtHelper::AddWidget, parent, child);
  }
  void InsertWidget(QWidget *parent, int32_t index, QWidget *child)
  {
    InvokeVoidFunction(&IMiniQtHelper::InsertWidget, parent, index, child);
  }

  // widget manipulation

  void SetWidgetText(QWidget *widget, const rdcstr &text)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetWidgetText, widget, text);
  }
  rdcstr GetWidgetText(QWidget *widget)
  {
    return InvokeRetFunction<rdcstr>(&IMiniQtHelper::GetWidgetText, widget);
  }

  void SetWidgetFont(QWidget *widget, const rdcstr &font, int32_t fontSize, bool bold, bool italic)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetWidgetFont, widget, font, fontSize, bold, italic);
  }

  void SetWidgetEnabled(QWidget *widget, bool enabled)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetWidgetEnabled, widget, enabled);
  }
  bool IsWidgetEnabled(QWidget *widget)
  {
    return InvokeRetFunction<bool>(&IMiniQtHelper::IsWidgetEnabled, widget);
  }
  void SetWidgetVisible(QWidget *widget, bool visible)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetWidgetVisible, widget, visible);
  }
  bool IsWidgetVisible(QWidget *widget)
  {
    return InvokeRetFunction<bool>(&IMiniQtHelper::IsWidgetVisible, widget);
  }

  // specific widgets

  QWidget *CreateGroupBox(bool collapsible)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateGroupBox, collapsible);
  }

  QWidget *CreateButton(WidgetCallback pressed)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateButton, pressed);
  }

  QWidget *CreateLabel() { return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateLabel); }
  void SetLabelImage(QWidget *widget, const bytebuf &data, int32_t width, int32_t height, bool alpha)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetLabelImage, widget, data, width, height, alpha);
  }
  QWidget *CreateOutputRenderingWidget()
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateOutputRenderingWidget);
  }
  WindowingData GetWidgetWindowingData(QWidget *widget)
  {
    return InvokeRetFunction<WindowingData>(&IMiniQtHelper::GetWidgetWindowingData, widget);
  }

  void SetWidgetReplayOutput(QWidget *widget, IReplayOutput *output)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetWidgetReplayOutput, widget, output);
  }

  void SetWidgetBackgroundColor(QWidget *widget, float red, float green, float blue)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetWidgetBackgroundColor, widget, red, green, blue);
  }
  QWidget *CreateCheckbox(WidgetCallback changed)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateCheckbox, changed);
  }
  QWidget *CreateRadiobox(WidgetCallback changed)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateRadiobox, changed);
  }

  void SetWidgetChecked(QWidget *checkableWidget, bool checked)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetWidgetChecked, checkableWidget, checked);
  }
  bool IsWidgetChecked(QWidget *checkableWidget)
  {
    return InvokeRetFunction<bool>(&IMiniQtHelper::IsWidgetChecked, checkableWidget);
  }

  QWidget *CreateSpinbox(int32_t decimalPlaces, double step)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateSpinbox, decimalPlaces, step);
  }

  void SetSpinboxBounds(QWidget *spinbox, double minVal, double maxVal)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetSpinboxBounds, spinbox, minVal, maxVal);
  }
  void SetSpinboxValue(QWidget *spinbox, double value)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetSpinboxValue, spinbox, value);
  }
  double GetSpinboxValue(QWidget *spinbox)
  {
    return InvokeRetFunction<double>(&IMiniQtHelper::GetSpinboxValue, spinbox);
  }

  QWidget *CreateTextBox(bool singleLine, WidgetCallback changed)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateTextBox, singleLine, changed);
  }

  QWidget *CreateComboBox(bool editable, WidgetCallback changed)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateComboBox, editable, changed);
  }

  void SetComboOptions(QWidget *combo, const rdcarray<rdcstr> &options)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetComboOptions, combo, options);
  }

  size_t GetComboCount(QWidget *combo)
  {
    return InvokeRetFunction<size_t>(&IMiniQtHelper::GetComboCount, combo);
  }

  void SelectComboOption(QWidget *combo, const rdcstr &option)
  {
    InvokeVoidFunction(&IMiniQtHelper::SelectComboOption, combo, option);
  }

  QWidget *CreateProgressBar(bool horizontal)
  {
    return InvokeRetFunction<QWidget *>(&IMiniQtHelper::CreateProgressBar, horizontal);
  }

  void ResetProgressBar(QWidget *pbar)
  {
    InvokeVoidFunction(&IMiniQtHelper::ResetProgressBar, pbar);
  }

  void SetProgressBarValue(QWidget *pbar, int32_t value)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetProgressBarValue, pbar, value);
  }

  void UpdateProgressBarValue(QWidget *pbar, int32_t delta)
  {
    InvokeVoidFunction(&IMiniQtHelper::UpdateProgressBarValue, pbar, delta);
  }

  int32_t GetProgressBarValue(QWidget *pbar)
  {
    return InvokeRetFunction<int>(&IMiniQtHelper::GetProgressBarValue, pbar);
  }

  void SetProgressBarRange(QWidget *pbar, int32_t minimum, int32_t maximum)
  {
    InvokeVoidFunction(&IMiniQtHelper::SetProgressBarRange, pbar, minimum, maximum);
  }

  int32_t GetProgressBarMinimum(QWidget *pbar)
  {
    return InvokeRetFunction<int>(&IMiniQtHelper::GetProgressBarMinimum, pbar);
  }

  int32_t GetProgressBarMaximum(QWidget *pbar)
  {
    return InvokeRetFunction<int>(&IMiniQtHelper::GetProgressBarMaximum, pbar);
  }
};

struct ExtensionInvoker : ObjectForwarder<IExtensionManager>
{
  MiniQtInvoker *m_MiniQt;
  ExtensionInvoker(PythonShell *shell, IExtensionManager &obj) : ObjectForwarder(shell, obj)
  {
    m_MiniQt = new MiniQtInvoker(shell, obj.GetMiniQtHelper());
  }
  virtual ~ExtensionInvoker() { delete m_MiniQt; }
  //
  ///////////////////////////////////////////////////////////////////////
  // pass-through functions that don't need the UI thread
  ///////////////////////////////////////////////////////////////////////
  //
  rdcarray<ExtensionMetadata> GetInstalledExtensions() { return m_Obj.GetInstalledExtensions(); }
  rdcarray<rdcstr> GetLoadedExtensions() { return m_Obj.GetLoadedExtensions(); }
  bool IsExtensionLoaded(rdcstr name) { return m_Obj.IsExtensionLoaded(name); }
  rdcstr LoadExtension(rdcstr name) { return m_Obj.LoadExtension(name); }
  bool IsPythonDebuggerConnected() { return m_Obj.IsPythonDebuggerConnected(); }
  IMiniQtHelper &GetMiniQtHelper() { return *m_MiniQt; }
  //
  ///////////////////////////////////////////////////////////////////////
  // functions that invoke onto the UI thread
  ///////////////////////////////////////////////////////////////////////
  //
  void RegisterWindowMenu(WindowMenu base, const rdcarray<rdcstr> &submenus,
                          ExtensionCallback callback)
  {
    InvokeVoidFunction(&IExtensionManager::RegisterWindowMenu, base, submenus, callback);
  }

  void RegisterPanelMenu(PanelMenu base, const rdcarray<rdcstr> &submenus, ExtensionCallback callback)
  {
    InvokeVoidFunction(&IExtensionManager::RegisterPanelMenu, base, submenus, callback);
  }

  void RegisterContextMenu(ContextMenu base, const rdcarray<rdcstr> &submenus,
                           ExtensionCallback callback)
  {
    InvokeVoidFunction(&IExtensionManager::RegisterContextMenu, base, submenus, callback);
  }

  void MessageDialog(const rdcstr &text, const rdcstr &title)
  {
    InvokeVoidFunction(&IExtensionManager::MessageDialog, text, title);
  }

  void ErrorDialog(const rdcstr &text, const rdcstr &title)
  {
    InvokeVoidFunction(&IExtensionManager::ErrorDialog, text, title);
  }

  DialogButton QuestionDialog(const rdcstr &text, const rdcarray<DialogButton> &options,
                              const rdcstr &title)
  {
    return InvokeRetFunction<DialogButton>(&IExtensionManager::QuestionDialog, text, options, title);
  }

  rdcstr OpenFileName(const rdcstr &caption, const rdcstr &dir, const rdcstr &filter)
  {
    return InvokeRetFunction<rdcstr>(&IExtensionManager::OpenFileName, caption, dir, filter);
  }

  rdcstr OpenDirectoryName(const rdcstr &caption, const rdcstr &dir)
  {
    return InvokeRetFunction<rdcstr>(&IExtensionManager::OpenDirectoryName, caption, dir);
  }

  rdcstr SaveFileName(const rdcstr &caption, const rdcstr &dir, const rdcstr &filter)
  {
    return InvokeRetFunction<rdcstr>(&IExtensionManager::SaveFileName, caption, dir, filter);
  }

  void MenuDisplaying(ContextMenu contextMenu, QMenu *menu, const ExtensionCallbackData &data)
  {
    InvokeVoidFunction(
        (void(IExtensionManager::*)(ContextMenu, QMenu *, const ExtensionCallbackData &)) &
            IExtensionManager::MenuDisplaying,
        contextMenu, menu, data);
  }
  void MenuDisplaying(PanelMenu panelMenu, QMenu *menu, QWidget *extensionButton,
                      const ExtensionCallbackData &data)
  {
    InvokeVoidFunction(
        (void(IExtensionManager::*)(PanelMenu, QMenu *, QWidget *, const ExtensionCallbackData &)) &
            IExtensionManager::MenuDisplaying,
        panelMenu, menu, extensionButton, data);
  }
};

struct CaptureContextInvoker : ObjectForwarder<ICaptureContext>
{
  ExtensionInvoker *m_Ext;
  CaptureContextInvoker(PythonShell *shell, ICaptureContext &obj) : ObjectForwarder(shell, obj)
  {
    m_Ext = new ExtensionInvoker(shell, obj.Extensions());
  }
  virtual ~CaptureContextInvoker() { delete m_Ext; }
  //
  ///////////////////////////////////////////////////////////////////////
  // pass-through functions that don't need the UI thread
  ///////////////////////////////////////////////////////////////////////
  //
  virtual rdcstr TempCaptureFilename(const rdcstr &appname) override
  {
    return m_Obj.TempCaptureFilename(appname);
  }
  virtual IExtensionManager &Extensions() override { return *m_Ext; }
  virtual IReplayManager &Replay() override { return m_Obj.Replay(); }
  virtual bool IsCaptureLoaded() override { return m_Obj.IsCaptureLoaded(); }
  virtual bool IsCaptureLocal() override { return m_Obj.IsCaptureLocal(); }
  virtual bool IsCaptureTemporary() override { return m_Obj.IsCaptureTemporary(); }
  virtual bool IsCaptureLoading() override { return m_Obj.IsCaptureLoading(); }
  virtual ResultDetails GetFatalError() override { return m_Obj.GetFatalError(); }
  virtual rdcstr GetCaptureFilename() override { return m_Obj.GetCaptureFilename(); }
  virtual CaptureModifications GetCaptureModifications() override
  {
    return m_Obj.GetCaptureModifications();
  }
  virtual const FrameDescription &FrameInfo() override { return m_Obj.FrameInfo(); }
  virtual const APIProperties &APIProps() override { return m_Obj.APIProps(); }
  virtual rdcarray<ShaderEncoding> TargetShaderEncodings() override
  {
    return m_Obj.TargetShaderEncodings();
  }
  virtual rdcarray<ShaderEncoding> CustomShaderEncodings() override
  {
    return m_Obj.CustomShaderEncodings();
  }
  virtual rdcarray<ShaderSourcePrefix> CustomShaderSourcePrefixes() override
  {
    return m_Obj.CustomShaderSourcePrefixes();
  }
  virtual uint32_t CurSelectedEvent() override { return m_Obj.CurSelectedEvent(); }
  virtual uint32_t CurEvent() override { return m_Obj.CurEvent(); }
  virtual const ActionDescription *CurSelectedAction() override
  {
    return m_Obj.CurSelectedAction();
  }
  virtual const ActionDescription *CurAction() override { return m_Obj.CurAction(); }
  virtual const ActionDescription *GetFirstAction() override { return m_Obj.GetFirstAction(); }
  virtual const ActionDescription *GetLastAction() override { return m_Obj.GetLastAction(); }
  virtual const rdcarray<ActionDescription> &CurRootActions() override
  {
    return m_Obj.CurRootActions();
  }
  virtual const ResourceDescription *GetResource(ResourceId id) const override
  {
    return m_Obj.GetResource(id);
  }
  virtual const rdcarray<ResourceDescription> &GetResources() override
  {
    return m_Obj.GetResources();
  }
  virtual rdcstr GetResourceName(ResourceId id) const override { return m_Obj.GetResourceName(id); }
  virtual rdcstr GetResourceNameUnsuffixed(ResourceId id) const override
  {
    return m_Obj.GetResourceNameUnsuffixed(id);
  }
  virtual bool IsAutogeneratedName(ResourceId id) override { return m_Obj.IsAutogeneratedName(id); }
  virtual bool HasResourceCustomName(ResourceId id) override
  {
    return m_Obj.HasResourceCustomName(id);
  }
  virtual int32_t ResourceNameCacheID() const override { return m_Obj.ResourceNameCacheID(); }
  virtual TextureDescription *GetTexture(ResourceId id) override { return m_Obj.GetTexture(id); }
  virtual const rdcarray<TextureDescription> &GetTextures() override { return m_Obj.GetTextures(); }
  virtual BufferDescription *GetBuffer(ResourceId id) override { return m_Obj.GetBuffer(id); }
  virtual DescriptorStoreDescription *GetDescriptorStore(ResourceId id) override
  {
    return m_Obj.GetDescriptorStore(id);
  }
  virtual const rdcarray<BufferDescription> &GetBuffers() const override
  {
    return m_Obj.GetBuffers();
  }
  virtual const ActionDescription *GetAction(uint32_t eventId) override
  {
    return m_Obj.GetAction(eventId);
  }
  virtual void ClearReplayCache() override { return m_Obj.ClearReplayCache(); }
  virtual bool OpenRGPProfile(const rdcstr &filename) override
  {
    return m_Obj.OpenRGPProfile(filename);
  }
  virtual IRGPInterop *GetRGPInterop() override { return m_Obj.GetRGPInterop(); }
  virtual const SDFile &GetStructuredFile() override { return m_Obj.GetStructuredFile(); }
  virtual WindowingSystem CurWindowingSystem() override { return m_Obj.CurWindowingSystem(); }
  virtual const rdcarray<DebugMessage> &DebugMessages() override { return m_Obj.DebugMessages(); }
  virtual int32_t UnreadMessageCount() override { return m_Obj.UnreadMessageCount(); }
  virtual void MarkMessagesRead() override { return m_Obj.MarkMessagesRead(); }
  virtual rdcstr GetNotes(const rdcstr &key) override { return m_Obj.GetNotes(key); }
  virtual rdcarray<EventBookmark> GetBookmarks() override { return m_Obj.GetBookmarks(); }
  virtual const D3D11Pipe::State *CurD3D11PipelineState() override
  {
    return m_Obj.CurD3D11PipelineState();
  }
  virtual const D3D12Pipe::State *CurD3D12PipelineState() override
  {
    return m_Obj.CurD3D12PipelineState();
  }
  virtual const GLPipe::State *CurGLPipelineState() override { return m_Obj.CurGLPipelineState(); }
  virtual const VKPipe::State *CurVulkanPipelineState() override
  {
    return m_Obj.CurVulkanPipelineState();
  }
  virtual const PipeState &CurPipelineState() override { return m_Obj.CurPipelineState(); }
  virtual PersistantConfig &Config() override { return m_Obj.Config(); }
  //
  ///////////////////////////////////////////////////////////////////////
  // functions that invoke onto the UI thread
  ///////////////////////////////////////////////////////////////////////
  //
  virtual void ConnectToRemoteServer(RemoteHost host) override
  {
    InvokeVoidFunction(&ICaptureContext::ConnectToRemoteServer, host);
  }
  virtual WindowingData CreateWindowingData(QWidget *window) override
  {
    return InvokeRetFunction<WindowingData>(&ICaptureContext::CreateWindowingData, window);
  }
  virtual void LoadCapture(const rdcstr &capture, const ReplayOptions &opts,
                           const rdcstr &origFilename, bool temporary, bool local) override
  {
    InvokeVoidFunction(&ICaptureContext::LoadCapture, capture, opts, origFilename, temporary, local);
  }
  virtual bool SaveCaptureTo(const rdcstr &capture) override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::SaveCaptureTo, capture);
  }
  virtual void RecompressCapture() override
  {
    InvokeVoidFunction(&ICaptureContext::RecompressCapture);
  }
  virtual void CloseCapture() override { InvokeVoidFunction(&ICaptureContext::CloseCapture); }
  virtual bool ImportCapture(const CaptureFileFormat &fmt, const rdcstr &importfile,
                             const rdcstr &rdcfile) override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::ImportCapture, fmt, importfile, rdcfile);
  }
  virtual void ExportCapture(const CaptureFileFormat &fmt, const rdcstr &exportfile) override
  {
    InvokeVoidFunction(&ICaptureContext::ExportCapture, fmt, exportfile);
  }
  virtual void SetEventID(const rdcarray<ICaptureViewer *> &exclude, uint32_t selectedEventID,
                          uint32_t eventId, bool force = false) override
  {
    InvokeVoidFunction(&ICaptureContext::SetEventID, exclude, selectedEventID, eventId, force);
  }
  virtual void RefreshStatus() override { InvokeVoidFunction(&ICaptureContext::RefreshStatus); }
  virtual bool IsResourceReplaced(ResourceId id) override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::IsResourceReplaced, id);
  }
  virtual ResourceId GetResourceReplacement(ResourceId id) override
  {
    return InvokeRetFunction<ResourceId>(&ICaptureContext::GetResourceReplacement, id);
  }
  virtual void RegisterReplacement(ResourceId from, ResourceId to) override
  {
    InvokeVoidFunction(&ICaptureContext::RegisterReplacement, from, to);
  }
  virtual void UnregisterReplacement(ResourceId id) override
  {
    InvokeVoidFunction(&ICaptureContext::UnregisterReplacement, id);
  }
  virtual void AddCaptureViewer(ICaptureViewer *viewer) override
  {
    InvokeVoidFunction(&ICaptureContext::AddCaptureViewer, viewer);
  }
  virtual void RemoveCaptureViewer(ICaptureViewer *viewer) override
  {
    InvokeVoidFunction(&ICaptureContext::RemoveCaptureViewer, viewer);
  }
  virtual void AddMessages(const rdcarray<DebugMessage> &msgs) override
  {
    InvokeVoidFunction(&ICaptureContext::AddMessages, msgs);
  }
  virtual void ClearMessages() override { InvokeVoidFunction(&ICaptureContext::ClearMessages); }
  virtual void SetResourceCustomName(ResourceId id, const rdcstr &name) override
  {
    InvokeVoidFunction(&ICaptureContext::SetResourceCustomName, id, name);
  }
  virtual void SetNotes(const rdcstr &key, const rdcstr &contents) override
  {
    InvokeVoidFunction(&ICaptureContext::SetNotes, key, contents);
  }

  virtual void SetBookmark(const EventBookmark &mark) override
  {
    InvokeVoidFunction(&ICaptureContext::SetBookmark, mark);
  }
  virtual void RemoveBookmark(uint32_t EID) override
  {
    InvokeVoidFunction(&ICaptureContext::RemoveBookmark, EID);
  }
  virtual void EmbedDependentFiles() override
  {
    InvokeVoidFunction(&ICaptureContext::EmbedDependentFiles);
  }
  virtual void RemoveDependentFiles() override
  {
    InvokeVoidFunction(&ICaptureContext::RemoveDependentFiles);
  }
  virtual void DelayedCallback(uint32_t milliseconds, std::function<void()> callback) override
  {
    InvokeVoidFunction(&ICaptureContext::DelayedCallback, milliseconds, callback);
  }
  virtual IMainWindow *GetMainWindow() override
  {
    return InvokeRetFunction<IMainWindow *>(&ICaptureContext::GetMainWindow);
  }
  virtual IEventBrowser *GetEventBrowser() override
  {
    return InvokeRetFunction<IEventBrowser *>(&ICaptureContext::GetEventBrowser);
  }
  virtual IAPIInspector *GetAPIInspector() override
  {
    return InvokeRetFunction<IAPIInspector *>(&ICaptureContext::GetAPIInspector);
  }
  virtual IAnnotationViewer *GetAnnotationViewer() override
  {
    return InvokeRetFunction<IAnnotationViewer *>(&ICaptureContext::GetAnnotationViewer);
  }
  virtual ITextureViewer *GetTextureViewer() override
  {
    return InvokeRetFunction<ITextureViewer *>(&ICaptureContext::GetTextureViewer);
  }
  virtual IBufferViewer *GetMeshPreview() override
  {
    return InvokeRetFunction<IBufferViewer *>(&ICaptureContext::GetMeshPreview);
  }
  virtual IPipelineStateViewer *GetPipelineViewer() override
  {
    return InvokeRetFunction<IPipelineStateViewer *>(&ICaptureContext::GetPipelineViewer);
  }
  virtual ICaptureDialog *GetCaptureDialog() override
  {
    return InvokeRetFunction<ICaptureDialog *>(&ICaptureContext::GetCaptureDialog);
  }
  virtual IDebugMessageView *GetDebugMessageView() override
  {
    return InvokeRetFunction<IDebugMessageView *>(&ICaptureContext::GetDebugMessageView);
  }
  virtual IDiagnosticLogView *GetDiagnosticLogView() override
  {
    return InvokeRetFunction<IDiagnosticLogView *>(&ICaptureContext::GetDiagnosticLogView);
  }
  virtual ICommentView *GetCommentView() override
  {
    return InvokeRetFunction<ICommentView *>(&ICaptureContext::GetCommentView);
  }
  virtual IPerformanceCounterViewer *GetPerformanceCounterViewer() override
  {
    return InvokeRetFunction<IPerformanceCounterViewer *>(
        &ICaptureContext::GetPerformanceCounterViewer);
  }
  virtual IStatisticsViewer *GetStatisticsViewer() override
  {
    return InvokeRetFunction<IStatisticsViewer *>(&ICaptureContext::GetStatisticsViewer);
  }
  virtual ITimelineBar *GetTimelineBar() override
  {
    return InvokeRetFunction<ITimelineBar *>(&ICaptureContext::GetTimelineBar);
  }
  virtual IPythonShell *GetPythonShell() override
  {
    return InvokeRetFunction<IPythonShell *>(&ICaptureContext::GetPythonShell);
  }
  virtual IResourceInspector *GetResourceInspector() override
  {
    return InvokeRetFunction<IResourceInspector *>(&ICaptureContext::GetResourceInspector);
  }
  virtual bool HasEventBrowser() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasEventBrowser);
  }
  virtual bool HasAPIInspector() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasAPIInspector);
  }
  virtual bool HasAnnotationViewer() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasAnnotationViewer);
  }
  virtual bool HasTextureViewer() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasTextureViewer);
  }
  virtual bool HasPipelineViewer() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasPipelineViewer);
  }
  virtual bool HasMeshPreview() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasMeshPreview);
  }
  virtual bool HasCaptureDialog() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasCaptureDialog);
  }
  virtual bool HasDebugMessageView() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasDebugMessageView);
  }
  virtual bool HasDiagnosticLogView() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasDiagnosticLogView);
  }
  virtual bool HasCommentView() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasCommentView);
  }
  virtual bool HasPerformanceCounterViewer() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasPerformanceCounterViewer);
  }
  virtual bool HasStatisticsViewer() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasStatisticsViewer);
  }
  virtual bool HasTimelineBar() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasTimelineBar);
  }
  virtual bool HasPythonShell() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasPythonShell);
  }
  virtual bool HasResourceInspector() override
  {
    return InvokeRetFunction<bool>(&ICaptureContext::HasResourceInspector);
  }

  virtual void ShowEventBrowser() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowEventBrowser);
  }
  virtual void ShowAPIInspector() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowAPIInspector);
  }
  virtual void ShowAnnotationViewer() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowAnnotationViewer);
  }
  virtual void ShowTextureViewer() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowTextureViewer);
  }
  virtual void ShowMeshPreview() override { InvokeVoidFunction(&ICaptureContext::ShowMeshPreview); }
  virtual void ShowPipelineViewer() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowPipelineViewer);
  }
  virtual void ShowCaptureDialog() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowCaptureDialog);
  }
  virtual void ShowDebugMessageView() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowDebugMessageView);
  }
  virtual void ShowDiagnosticLogView() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowDiagnosticLogView);
  }
  virtual void ShowCommentView() override { InvokeVoidFunction(&ICaptureContext::ShowCommentView); }
  virtual void ShowPerformanceCounterViewer() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowPerformanceCounterViewer);
  }
  virtual void ShowStatisticsViewer() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowStatisticsViewer);
  }
  virtual void ShowTimelineBar() override { InvokeVoidFunction(&ICaptureContext::ShowTimelineBar); }
  virtual void ShowPythonShell() override { InvokeVoidFunction(&ICaptureContext::ShowPythonShell); }
  virtual void ShowResourceInspector() override
  {
    InvokeVoidFunction(&ICaptureContext::ShowResourceInspector);
  }
  virtual IShaderViewer *EditShader(ResourceId id, ShaderStage stage, const rdcstr &entryPoint,
                                    const rdcstrpairs &files, KnownShaderTool knownTool,
                                    ShaderEncoding shaderEncoding, ShaderCompileFlags flags,
                                    IShaderViewer::SaveCallback saveCallback,
                                    IShaderViewer::RevertCallback revertCallback) override
  {
    return InvokeRetFunction<IShaderViewer *>(&ICaptureContext::EditShader, id, stage, entryPoint,
                                              files, knownTool, shaderEncoding, flags, saveCallback,
                                              revertCallback);
  }

  virtual IShaderViewer *DebugShader(const ShaderReflection *shader, ResourceId pipeline,
                                     ShaderDebugTrace *trace, const rdcstr &debugContext) override
  {
    return InvokeRetFunction<IShaderViewer *>(&ICaptureContext::DebugShader, shader, pipeline,
                                              trace, debugContext);
  }

  virtual IShaderViewer *ViewShader(const ShaderReflection *shader, ResourceId pipeline) override
  {
    return InvokeRetFunction<IShaderViewer *>(&ICaptureContext::ViewShader, shader, pipeline);
  }

  virtual IShaderMessageViewer *ViewShaderMessages(ShaderStageMask stages) override
  {
    return InvokeRetFunction<IShaderMessageViewer *>(&ICaptureContext::ViewShaderMessages, stages);
  }

  virtual IDescriptorViewer *ViewDescriptorStore(ResourceId id) override
  {
    return InvokeRetFunction<IDescriptorViewer *>(&ICaptureContext::ViewDescriptorStore, id);
  }
  virtual IDescriptorViewer *ViewDescriptors(const rdcarray<Descriptor> &descriptors,
                                             const rdcarray<SamplerDescriptor> &samplerDescriptors) override
  {
    return InvokeRetFunction<IDescriptorViewer *>(&ICaptureContext::ViewDescriptors, descriptors,
                                                  samplerDescriptors);
  }

  virtual IBufferViewer *ViewBuffer(uint64_t byteOffset, uint64_t byteSize, ResourceId id,
                                    const rdcstr &format = "") override
  {
    return InvokeRetFunction<IBufferViewer *>(&ICaptureContext::ViewBuffer, byteOffset, byteSize,
                                              id, format);
  }

  virtual IBufferViewer *ViewTextureAsBuffer(ResourceId id, const Subresource &sub,
                                             const rdcstr &format = "") override
  {
    return InvokeRetFunction<IBufferViewer *>(&ICaptureContext::ViewTextureAsBuffer, id, sub, format);
  }

  virtual IBufferViewer *ViewConstantBuffer(ShaderStage stage, uint32_t slot, uint32_t idx) override
  {
    return InvokeRetFunction<IBufferViewer *>(&ICaptureContext::ViewConstantBuffer, stage, slot, idx);
  }

  virtual IPixelHistoryView *ViewPixelHistory(ResourceId texID, uint32_t x, uint32_t y,
                                              uint32_t view, const TextureDisplay &display) override
  {
    return InvokeRetFunction<IPixelHistoryView *>(&ICaptureContext::ViewPixelHistory, texID, x, y,
                                                  view, display);
  }

  virtual QWidget *CreateBuiltinWindow(const rdcstr &objectName) override
  {
    return InvokeRetFunction<QWidget *>(&ICaptureContext::CreateBuiltinWindow, objectName);
  }

  virtual void BuiltinWindowClosed(QWidget *window) override
  {
    InvokeVoidFunction(&ICaptureContext::BuiltinWindowClosed, window);
  }

  virtual void RaiseDockWindow(QWidget *dockWindow) override
  {
    InvokeVoidFunction(&ICaptureContext::RaiseDockWindow, dockWindow);
  }

  virtual void AddDockWindow(QWidget *newWindow, DockReference ref, QWidget *refWindow,
                             float percentage = 0.5f) override
  {
    InvokeVoidFunction(&ICaptureContext::AddDockWindow, newWindow, ref, refWindow, percentage);
  }
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

PythonShell::PythonShell(ICaptureContext &ctx, QWidget *parent)
    : QFrame(parent), ui(new Ui::PythonShell), m_Ctx(ctx)
{
  ui->setupUi(this);

  m_ThreadCtx = new CaptureContextInvoker(this, m_Ctx);

  QObject::connect(ui->lineInput, &RDLineEdit::keyPress, this, &PythonShell::interactive_keypress);
  QObject::connect(ui->helpSearch, &RDLineEdit::keyPress, this, &PythonShell::helpSearch_keypress);

  ui->lineInput->setFont(Formatter::FixedFont());
  ui->interactiveOutput->setFont(Formatter::FixedFont());
  ui->scriptOutput->setFont(Formatter::FixedFont());
  ui->helpText->setFont(Formatter::FixedFont());

  ui->outputGroup->setWindowTitle(tr("Output"));
  ui->helpGroup->setWindowTitle(tr("Help"));
  ui->replGroup->setWindowTitle(tr("Interactive REPL"));

  ui->lineInput->setAcceptTabCharacters(true);

  QObject::connect(ui->interactiveOutput, &RDTextEdit::keyPress, [this](QKeyEvent *e) {
    // ignore keypresses that aren't typing, but for up/down redirect that to the line input to get history
    if((e->text().isEmpty() || !e->text()[0].isPrint()) && e->key() != Qt::Key_Up &&
       e->key() != Qt::Key_Down)
      return;
    ui->lineInput->setFocus(Qt::OtherFocusReason);
    QApplication::postEvent(ui->lineInput, new QKeyEvent(*e));
  });

  // reset output to default
  on_clear_clicked();
  on_newScript_clicked();

  ScintillaEdit *editor = m_Scintillas[0];

  ui->saveScript->setEnabled(false);

  ToolWindowManagerArea *editorTabs = ui->docking->areaOf(editor);
  QObject::connect(editorTabs, &QTabWidget::currentChanged, this, &PythonShell::editorTab_Changed);

  ui->docking->addToolWindow(ui->replGroup, ToolWindowManager::AreaReference(
                                                ToolWindowManager::BottomOf, editorTabs, 0.3f));
  ui->docking->setToolWindowProperties(
      ui->replGroup, ToolWindowManager::HideCloseButton | ToolWindowManager::DisallowFloatWindow);

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

  for(ScintillaEdit *edit : m_Scintillas)
    delete edit;

  interactiveContext->Finish();

  delete m_ThreadCtx;

  delete ui;
}

void PythonShell::editorTab_Changed(int index)
{
  ScintillaEdit *editor = curEditor();

  ui->saveScript->setEnabled(getEditorFilename(editor) != QString());
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

ScintillaEdit *PythonShell::makeEditor()
{
  ScintillaEdit *editor = new EditorWrapper(this);

  editor->indicSetFore(0, 0x0000ff);

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

  ConfigureSyntax(editor, SCLEX_PYTHON);

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
    m_Scintillas.removeOne(editor);
    updateEditorCloseButton();
  });

  QObject::connect(editor, &ScintillaEdit::modified,
                   [this, editor](int type, int, int, int, const QByteArray &text, int, int, int) {
                     if(type & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT | SC_MOD_BEFOREINSERT |
                                SC_MOD_BEFOREDELETE))
                     {
                       markEditorModified(editor, true);
                       updateEditorTitle(editor);

                       editor->markerDeleteAll(CURRENT_MARKER);
                       editor->markerDeleteAll(CURRENT_MARKER + 1);

                       // always remove errors immediately
                       editor->setIndicatorCurrent(0);
                       editor->indicatorClearRange(0, editor->textLength());
                       editor->annotationClearAll();
                     }
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

QVariant PythonShell::persistData()
{
  QVariantMap state = ui->docking->saveState();

  return state;
}

void PythonShell::setPersistData(const QVariant &persistData)
{
  QVariantMap state = persistData.toMap();

  ui->docking->restoreState(state);

  ToolWindowManagerArea *editorTabs = ui->docking->areaOf(m_Scintillas[0]);
  QObject::connect(editorTabs, &QTabWidget::currentChanged, this, &PythonShell::editorTab_Changed);
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
      ScintillaEdit *editor = makeEditor();

      editor->setText(f.readAll().data());
      editor->emptyUndoBuffer();

      ui->saveScript->setEnabled(true);

      setEditorFilename(editor, QString(filename));
      markEditorModified(editor, false);
      return true;
    }
  }

  return false;
}

void PythonShell::CreateNewScriptEditor(rdcstr name, rdcstr text)
{
  ScintillaEdit *editor = makeEditor();

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

  LambdaThread *thread = new LambdaThread([this, debugging, script, context, editor]() {
    PythonContext::AddDebuggableThread();

    scriptContext = context;
    runningScriptEditor = editor;
    context->executeString(lit("script.py"), script, debugging);
    scriptContext = NULL;

    GUIInvoke::call(this, [this, context]() {
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

void PythonShell::on_execute_clicked()
{
  QString command = ui->lineInput->text();

  ANALYTIC_SET(UIFeatures.PythonInterop, true);

  appendText(ui->interactiveOutput, command + lit("\n"));

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
}

void PythonShell::on_newScript_clicked()
{
  QString minidocHeader = scriptHeader();

  minidocHeader.replace(QLatin1Char('\n'), lit("\n# "));

  minidocHeader = QFormatStr("# %1\n\n").arg(minidocHeader);

  ScintillaEdit *editor = makeEditor();

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

  runningScriptEditor->markerDeleteAll(CURRENT_MARKER);
  runningScriptEditor->markerDeleteAll(CURRENT_MARKER + 1);

  runningScriptEditor->markerAdd(line > 0 ? line - 1 : 0, CURRENT_MARKER);
  runningScriptEditor->markerAdd(line > 0 ? line - 1 : 0, CURRENT_MARKER + 1);
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
  if(!filename.isEmpty())
  {
    QDir dirinfo = QFileInfo(filename).dir();
    if(dirinfo.exists())
    {
      QFile f(filename);
      if(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      {
        QString text = QString::fromUtf8(editor->getText(editor->textLength() + 1));
        text.remove(QLatin1Char('\r'));
        f.write(text.toUtf8());

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

  QMenu contextMenu(this);

  QString typeName;

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
}

void PythonShell::selectedHelp(QString word)
{
}

void PythonShell::refreshCurrentHelp()
{
}

void PythonShell::interactive_keypress(QKeyEvent *event)
{
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
  ret->setGlobal("pyrenderdoc", (ICaptureContext *)m_ThreadCtx);
}
