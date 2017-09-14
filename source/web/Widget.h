/**
 *  @note This file is part of Empirical, https://github.com/devosoft/Empirical
 *  @copyright Copyright (C) Michigan State University, MIT Software license; see doc/LICENSE.md
 *  @date 2015-2017
 *
 *  @file  Widget.h
 *  @brief Widgets maintain individual components on a web page and link to Elements
 *
 *  Each HTML Widget has all of its details stored in a WidgetInfo object; Multiple Widgets can
 *  be attached to the same WidgetInfo, simplifying the usage.  All the library user needs to
 *  worry about is the Widget object itself; the WidgetInfo will be managed behind the scenes.
 *
 *  WidgetInfo contains the basic information for all Widgets
 *  Widget is a generic base class, with a shared pointer to WidgetInfo
 *  WidgetFacet is a template that allows Set* methods to return derived return-type.
 *
 *  In other files, Widgets will be used to define specific elements.
 *  ELEMENTInfo maintains information about the specific widget (derived from WidgetInfo)
 *  ELEMENT interfaces to ELEMENTInfo so multiple elements use same core; derived from WidgetFacet
 *
 *  Library users should not need to access Widgets directly, only specific derived types.
 *
 *  Tips for using widgets:
 *
 *  1. If you are about to make a lot of changes at once, run Freeze(), make the changes, and
 *     then run Activate() again.  Freeze prevents widgets from being updated immediately.
 *
 *  2. Trust the Widget to handle all of the manipulation behind the scenes
 *
 */


#ifndef EMP_WEB_WIDGET_H
#define EMP_WEB_WIDGET_H

#include <string>

#include "../base/vector.h"
#include "../tools/mem_track.h"

#include "events.h"
#include "init.h"
#include "WidgetExtras.h"

namespace emp {
namespace web {

  // Setup some types we will need later
  namespace internal {
    // Pre-declare WidgetInfo so classes can inter-operate.
    class WidgetInfo;
    class DivInfo;
    class TableInfo;

    /// Quick method for generating unique Widget ID numbers when not otherwise specified.
    static size_t NextWidgetNum(bool inc_num=true) {
      static size_t next_id = 0;
      if (!inc_num) return next_id;
      return next_id++;
    }

    /// Quick method for generating unique string IDs for Widgets.
    static std::string NextWidgetID() {
      return emp::to_string("emp__", NextWidgetNum());
    }

    /// Base class for command-objects that can be fed into widgets.
    class WidgetCommand {
    public:
      virtual ~WidgetCommand() { ; }
      virtual bool Trigger(WidgetInfo &) const = 0;
    };
  }


  /// Widget is effectively a smart pointer to a WidgetInfo object, plus some basic accessors.
  class Widget {
    friend internal::WidgetInfo; friend internal::DivInfo; friend internal::TableInfo;
  protected:
    using WidgetInfo = internal::WidgetInfo;
    WidgetInfo * info;                        ///< Information associated with this widget.

    /// If an Append doesn't work with current class, forward it to the parent and try there.
    template <typename FWD_TYPE> Widget & ForwardAppend(FWD_TYPE && arg);

    /// Set the information associated with this widget.
    Widget & SetInfo(WidgetInfo * in_info);

    /// Internally, we can treat a Widget as a pointer to its WidgetInfo.
    WidgetInfo * operator->() { return info; }

    /// Give derived classes the ability to access widget info.
    static WidgetInfo * Info(const Widget & w) { return w.info; }

    /// Four activity states for any widget:
    ///   INACTIVE - Not be in DOM at all.
    ///   WAITING  - Will become active once the page finishes loading.
    ///   FROZEN   - Part of DOM, but not updating on the screen.
    ///   ACTIVE   - Fully active; changes are reflected as they happen.

    enum ActivityState { INACTIVE, WAITING, FROZEN, ACTIVE };

    /// Default name for un-initialized widgets.
    static const std::string no_name;

  public:
    /// When Widgets are first created, they should be provided with an ID.
    Widget(const std::string & id);
    Widget(WidgetInfo * in_info=nullptr);
    Widget(const Widget & in) : Widget(in.info) { ; }
    Widget & operator=(const Widget & in) { return SetInfo(in.info); }

    virtual ~Widget();

    /// Test if this widget is valid.
    bool IsNull() const { return info == nullptr; }

    /// Some debugging helpers...
    std::string InfoTypeName() const;

    bool IsInactive() const;  ///< Test if the activity state of this widget is currently INACTIVE
    bool IsWaiting() const;   ///< Test if the activity state of this widget is currently WAITING
    bool IsFrozen() const;    ///< Test if the activity state of this widget is currently FROZEN
    bool IsActive() const;    ///< Test if the activity state of this widget is currently ACTIVE

    bool AppendOK() const;    ///< Is it okay to add more internal Widgets into this one?
    void PreventAppend();     ///< Disallow further appending to this Widget.

    bool IsButton() const;    ///< Is this Widget a Button?
    bool IsCanvas() const;    ///< Is this Widget a Canvas?
    bool IsImage() const;     ///< Is this Widget an Image?
    bool IsSelector() const;  ///< Is this Widget a Selector?
    bool IsDiv() const;       ///< Is this Widget a Div?
    bool IsTable() const;     ///< Is this Widget a Table?
    bool IsText() const;      ///< Is this Widget a Text?

    const std::string & GetID() const;  ///< What is the HTML string ID for this Widget?

    /// Retrieve a specific CSS trait associated with this Widget.
    /// Note: CSS-related options may be overridden in derived classes that have multiple styles.
    virtual std::string GetCSS(const std::string & setting);

    /// Determine is a CSS trait has been set on this Widget.
    virtual bool HasCSS(const std::string & setting);

    /// Retrieve a specific attribute associated with this Widget.
    virtual std::string GetAttr(const std::string & setting);

    /// Determine is an attribute has been set on this Widget.
    virtual bool HasAttr(const std::string & setting);

    /// Are two Widgets refering to the same HTML object?
    bool operator==(const Widget & in) const { return info == in.info; }

    /// Are two Widgets refering to differnt HTML objects?
    bool operator!=(const Widget & in) const { return info != in.info; }

    /// Conver Widget to bool (I.e., is this Widget active?)
    operator bool() const { return info != nullptr; }

    double GetXPos();          ///< Get the X-position of this Widget within its parent.
    double GetYPos();          ///< Get the Y-position of this Widget within its parent.
    double GetWidth();         ///< Get the width of this Widget on screen.
    double GetHeight();        ///< Get the height of this Widget on screen.
    double GetInnerWidth();    ///< Get the width of this Widget not including padding.
    double GetInnerHeight();   ///< Get the height of this Widget not including padding.
    double GetOuterWidth();    ///< Get the width of this Widget including all padding.
    double GetOuterHeight();   ///< Get the height of this Widget including all padding.

    /// Make this widget live, so changes occur immediately (once document is ready)
    void Activate();

    /// Record changes internally, but keep static screen until Activate() is called.
    void Freeze();

    /// Record changes internally and REMOVE from screen until Activate is called.
    /// (Argument is for recursive, internal use only.)
    virtual void Deactivate(bool top_level=true);

    /// Doggle between Active and Deactivated.
    bool ToggleActive();

    /// Clear and redraw the current widget on the screen.
    void Redraw();

    /// Look up previously created elements, by type.
    Widget & Find(const std::string & test_name);

    /// Add a dependant to this Widget that should be redrawn when it is.
    Widget & AddDependant(const Widget & w);

    /// Setup << operator to redirect to Append; option preparation can be overridden.
    virtual void PrepareAppend() { ; }
    template <typename IN_TYPE> Widget operator<<(IN_TYPE && in_val);

    /// Debug...
    std::string GetInfoType() const;
  };

  namespace internal {

    // WidgetInfo is a base class containing information needed by all GUI widget classes
    // (Buttons, Images, etc...).  It take in a return type to be cast to for accessors.

    class WidgetInfo {
    public:
      // Smart-pointer info
      int ptr_count;                  ///< How many widgets are pointing to this info?

      // Basic info about a widget
      std::string id;                 ///< ID used for associated DOM element.
      WidgetExtras extras;            ///< HTML attributes, CSS style, and listeners for web events.

      // Track hiearchy
      WidgetInfo * parent;            ///< Which WidgetInfo is this one contained within?
      emp::vector<Widget> dependants; ///< Widgets to be refreshed if this one is triggered
      Widget::ActivityState state;    ///< Is this element active in DOM?


      /// WidgetInfo cannot be built unless within derived class, so constructor is protected
      WidgetInfo(const std::string & in_id="")
        : ptr_count(1), id(in_id), parent(nullptr), state(Widget::INACTIVE)
      {
        EMP_TRACK_CONSTRUCT(WebWidgetInfo);
        if (id == "") id = NextWidgetID();
      }

      /// No copies of INFO allowed
      WidgetInfo(const WidgetInfo &) = delete;
      WidgetInfo & operator=(const WidgetInfo &) = delete;

      virtual ~WidgetInfo() {
        EMP_TRACK_DESTRUCT(WebWidgetInfo);
      }

      /// Debugging helpers...
      virtual std::string TypeName() const { return "WidgetInfo base"; }

      virtual bool IsButtonInfo() const { return false; }
      virtual bool IsCanvasInfo() const { return false; }
      virtual bool IsImageInfo() const { return false; }
      virtual bool IsSelectorInfo() const { return false; }
      virtual bool IsDivInfo() const { return false; }
      virtual bool IsTableInfo() const { return false; }
      virtual bool IsTextInfo() const { return false; }
      virtual bool IsTextAreaInfo() const { return false; }
      virtual bool IsD3VisualiationInfo() const { return false; }

      // If not overloaded, pass along widget registration to parent.
      virtual void Register_recurse(Widget & w) { if (parent) parent->Register_recurse(w); }
      virtual void Register(Widget & w) { if (parent) parent->Register(w); }
      virtual void Unregister_recurse(Widget & w) { if (parent) parent->Unregister_recurse(w); }
      virtual void Unregister(Widget & w) { if (parent) parent->Unregister(w); }

      // Some nodes can have children and need to be able to recursively register them.
      virtual void RegisterChildren(DivInfo * registrar) { ; }   // No children by default.
      virtual void UnregisterChildren(DivInfo * regestrar) { ; } // No children by default.

      // Record dependants.  Dependants are only acted upon when this widget's action is
      // triggered (e.g. a button is pressed)
      void AddDependant(Widget in) {
        dependants.emplace_back(in);
      }

      template <typename... T>
      void AddDependants(Widget first, T... widgets) {
        AddDependant(first);
        AddDependants(widgets...);
      }

      void AddDependants() { ; }

      void UpdateDependants() { for (auto & d : dependants) d->ReplaceHTML(); }


      // Activate is delayed until the document is ready, when DoActivate will be called.
      virtual void DoActivate(bool top_level=true) {
        state = Widget::ACTIVE;         // Activate this widget and its children.
        if (top_level) ReplaceHTML();   // Print full contents to document.
      }

      virtual bool AppendOK() const { return false; } // Most widgets can't be appended to.
      virtual void PreventAppend() { emp_assert(false, TypeName()); } // Only for appendable widgets.

      // By default, elements should forward unknown appends to their parents.
      virtual Widget Append(const std::string & text) { return ForwardAppend(text); }
      virtual Widget Append(const std::function<std::string()> & fn) { return ForwardAppend(fn); }
      virtual Widget Append(Widget info) { return ForwardAppend(info); }

      // Convert arbitrary inputs to a string and try again!
      virtual Widget Append(char in_char) { return Append(emp::to_string(in_char)); }
      virtual Widget Append(double in_num) { return Append(emp::to_string(in_num)); }
      virtual Widget Append(int in_num) { return Append(emp::to_string(in_num)); }
      virtual Widget Append(uint32_t in_num) { return Append(emp::to_string(in_num)); }

      // Handle special commands
      virtual Widget Append(const emp::web::internal::WidgetCommand & cmd) {
        if (cmd.Trigger(*this)) return Widget(this);
        return ForwardAppend(cmd);  // Otherwise pass the Close to parent!
      }


      // If an Append doesn't work with current class, forward it to the parent.
      template <typename FWD_TYPE>
      Widget ForwardAppend(FWD_TYPE && arg) {
        emp_assert(parent && "Trying to forward append to parent, but no parent!", id);
        return parent->Append(std::forward<FWD_TYPE>(arg));
      }

      // All derived widgets must suply a mechanism for providing associated HTML code.
      virtual void GetHTML(std::stringstream & ss) = 0;

      // Derived widgets may also provide JavaScript code to be run on redraw.
      virtual void TriggerJS() { ; }

      // Assume that the associated ID exists and replace it with the current HTML code.
      virtual void ReplaceHTML() {
        // If this node is frozen, don't change it!
        if (state == Widget::FROZEN) return;

        // If this node is active, fill put its contents in ss; otherwise make ss an empty span.
        std::stringstream ss;
        if (state == Widget::ACTIVE) GetHTML(ss);
        else ss << "<span id='" << id << "'></span>";

        // Now do the replacement.
        EM_ASM_ARGS({
            var widget_id = Pointer_stringify($0);
            var out_html = Pointer_stringify($1);
            $('#' + widget_id).replaceWith(out_html);
          }, id.c_str(), ss.str().c_str());

        // If active update style, trigger JS, and recurse to children!
        if (state == Widget::ACTIVE) {
          extras.Apply(id); // Update the attributes, style, and listeners.
          TriggerJS();      // Run associated Javascript code, if any (e.g., to fill out a canvas)
        }
      }

    public:
      virtual std::string GetType() { return "web::WidgetInfo"; }
    };

  }  // end namespaceinternal

  // Implementation of Widget methods...

  Widget::Widget(const std::string & id) {
    emp_assert(has_whitespace(id) == false);
    // We are creating a new widget; in derived class, make sure:
    // ... to assign info pointer to new object of proper *Info type
    // ... NOT to increment info->ptr_count since it's initialized to 1.
    EMP_TRACK_CONSTRUCT(WebWidget);
  }

  Widget::Widget(WidgetInfo * in_info) {
    info = in_info;
    if (info) info->ptr_count++;
    EMP_TRACK_CONSTRUCT(WebWidget);
  }

  Widget::~Widget() {
    // We are deleting a widget.
    if (info) {
      info->ptr_count--;
      if (info->ptr_count == 0) delete info;
    }
    EMP_TRACK_DESTRUCT(WebWidget);
  }

  std::string Widget::InfoTypeName() const { if (IsNull()) return "NULL"; return info->TypeName(); }

  Widget & Widget::SetInfo(WidgetInfo * in_info) {
    // If the widget is already set correctly, stop here.
    if (info == in_info) return *this;

    // Clean up the old info that was previously pointed to.
    if (info) {
      info->ptr_count--;
      if (info->ptr_count == 0) delete info;
    }

    // Setup new info.
    info = in_info;
    if (info) info->ptr_count++;

    return *this;
  }

  bool Widget::IsInactive() const { if (!info) return false; return info->state == INACTIVE; }
  bool Widget::IsWaiting() const { if (!info) return false; return info->state == WAITING; }
  bool Widget::IsFrozen() const { if (!info) return false; return info->state == FROZEN; }
  bool Widget::IsActive() const { if (!info) return false; return info->state == ACTIVE; }

  bool Widget::AppendOK() const { if (!info) return false; return info->AppendOK(); }
  void Widget::PreventAppend() { emp_assert(info); info->PreventAppend(); }

  const std::string Widget::no_name = "(none)";
  const std::string & Widget::GetID() const { return info ? info->id : no_name; }

  bool Widget::IsButton() const { if (!info) return false; return info->IsButtonInfo(); }
  bool Widget::IsCanvas() const { if (!info) return false; return info->IsCanvasInfo(); }
  bool Widget::IsImage() const { if (!info) return false; return info->IsImageInfo(); }
  bool Widget::IsSelector() const { if (!info) return false; return info->IsSelectorInfo(); }
  bool Widget::IsDiv() const { if (!info) return false; return info->IsDivInfo(); }
  bool Widget::IsTable() const { if (!info) return false; return info->IsTableInfo(); }
  bool Widget::IsText() const { if (!info) return false; return info->IsTextInfo(); }

  std::string Widget::GetCSS(const std::string & setting) {
    return info ? info->extras.GetStyle(setting) : "";
  }
  bool Widget::HasCSS(const std::string & setting) {
    return info ? info->extras.HasStyle(setting) : false;
  }

  std::string Widget::GetAttr(const std::string & setting) {
    return info ? info->extras.GetAttr(setting) : "";
  }
  bool Widget::HasAttr(const std::string & setting) {
    return info ? info->extras.HasAttr(setting) : false;
  }

  double Widget::GetXPos() {
    if (!info) return -1.0;
    return EM_ASM_DOUBLE({
      var id = Pointer_stringify($0);
      var rect = $('#' + id).position();
      return rect.left;
    }, GetID().c_str());
  }

  double Widget::GetYPos() {
    if (!info) return -1.0;
    return EM_ASM_DOUBLE({
      var id = Pointer_stringify($0);
      var rect = $('#' + id).position();
      return rect.top;
    }, GetID().c_str());
  }

  double Widget::GetWidth(){
    if (!info) return -1.0;
    return EM_ASM_DOUBLE({
      var id = Pointer_stringify($0);
      return $('#' + id).width();
    }, GetID().c_str());
  }
  double Widget::GetHeight(){
    if (!info) return -1.0;
    return EM_ASM_DOUBLE({
      var id = Pointer_stringify($0);
      return $('#' + id).height();
    }, GetID().c_str());
  }
  double Widget::GetInnerWidth(){
    if (!info) return -1.0;
    return EM_ASM_DOUBLE({
      var id = Pointer_stringify($0);
      return $('#' + id).innerWidth();
    }, GetID().c_str());
  }
  double Widget::GetInnerHeight(){
    if (!info) return -1.0;
    return EM_ASM_DOUBLE({
      var id = Pointer_stringify($0);
      return $('#' + id).innerHeight();
    }, GetID().c_str());
  }
  double Widget::GetOuterWidth(){
    if (!info) return -1.0;
    return EM_ASM_DOUBLE({
      var id = Pointer_stringify($0);
      return $('#' + id).outerWidth();
    }, GetID().c_str());
  }
  double Widget::GetOuterHeight(){
    if (!info) return -1.0;
    return EM_ASM_DOUBLE({
      var id = Pointer_stringify($0);
      return $('#' + id).outerHeight();
    }, GetID().c_str());
  }

  void Widget::Activate() {
    auto * cur_info = info;
    info->state = WAITING;
    OnDocumentReady( std::function<void(void)>([cur_info](){ cur_info->DoActivate(); }) );
  }

  void Widget::Freeze() {
    info->state = FROZEN;
  }

  void Widget::Deactivate(bool top_level) {
    if (!info || info->state == INACTIVE) return;  // Skip if we are not active.
    info->state = INACTIVE;
    if (top_level) info->ReplaceHTML();            // If at top level, clear the contents
  }

  bool Widget::ToggleActive() {
    emp_assert(info);
    if (info->state != INACTIVE) Deactivate();
    else Activate();
    return info->state;
  }

  void Widget::Redraw() {
    emp_assert(info);
    info->ReplaceHTML();
  }

  Widget & Widget::AddDependant(const Widget & w) {
    info->AddDependant(w);
    return *this;
  }

  template <typename IN_TYPE>
  Widget Widget::operator<<(IN_TYPE && in_val) {
    PrepareAppend();
    return info->Append(std::forward<IN_TYPE>(in_val));
  }

  std::string Widget::GetInfoType() const {
    if (!info) return "UNINITIALIZED";
    return info->GetType();
  }


  namespace internal {

    /// WidgetFacet is a template that provides accessors into Widget with a derived return type.
    template <typename RETURN_TYPE>
    class WidgetFacet : public Widget {
    protected:
      /// WidgetFacet cannot be built unless within derived class, so constructors are protected
      WidgetFacet(const std::string & in_id="") : Widget(in_id) { ; }
      WidgetFacet(const WidgetFacet & in) : Widget(in) { ; }
      WidgetFacet(const Widget & in) : Widget(in) {
        // Converting from a generic widget; make sure type is correct or non-existant!
        emp_assert(!in || dynamic_cast<typename RETURN_TYPE::INFO_TYPE *>( Info(in) ) != NULL,
                   in.GetID());
      }
      WidgetFacet(WidgetInfo * in_info) : Widget(in_info) { ; }
      WidgetFacet & operator=(const WidgetFacet & in) { Widget::operator=(in); return *this; }
      virtual ~WidgetFacet() { ; }

      /// CSS-related options may be overridden in derived classes that have multiple styles.
      /// By default DoCSS will track the new information and apply it (if active) to the widget.
      virtual void DoCSS(const std::string & setting, const std::string & value) {
        info->extras.style.DoSet(setting, value);
        if (IsActive()) Style::Apply(info->id, setting, value);
      }
      /// Attr-related options may be overridden in derived classes that have multiple attributes.
      /// By default DoAttr will track the new information and apply it (if active) to the widget.
      virtual void DoAttr(const std::string & setting, const std::string & value) {
        info->extras.attr.DoSet(setting, value);
        if (IsActive()) Attributes::Apply(info->id, setting, value);
      }
      /// Listener options may be overridden in derived classes that have multiple listen targets.
      /// By default DoListen will track new listens and set them up immediately, if active.
      virtual void DoListen(const std::string & event_name, size_t fun_id) {
        info->extras.listen.Set(event_name, fun_id);
        if (IsActive()) Listeners::Apply(info->id, event_name, fun_id);
      }

    public:
      using return_t = RETURN_TYPE;

      /// Set a specific CSS value for this widget.
      template <typename SETTING_TYPE>
      return_t & SetCSS(const std::string & setting, SETTING_TYPE && value) {
        emp_assert(info != nullptr);
        DoCSS(setting, emp::to_string(value));
        return (return_t &) *this;
      }

      /// Set a specific Attribute value for this widget.
      template <typename SETTING_TYPE>
      return_t & SetAttr(const std::string & setting, SETTING_TYPE && value) {
        emp_assert(info != nullptr);
        DoAttr(setting, emp::to_string(value));
        return (return_t &) *this;
      }

      /// Multiple CSS settings can be provided simultaneously.
      template <typename T1, typename T2, typename... OTHER_SETTINGS>
      return_t & SetCSS(const std::string & setting1, T1 && val1,
                        const std::string & setting2, T2 && val2,
                        OTHER_SETTINGS... others) {
        SetCSS(setting1, val1);                      // Set the first CSS value.
        return SetCSS(setting2, val2, others...);    // Recurse to the others.
      }

      /// Multiple Attributes can be provided simultaneously.
      template <typename T1, typename T2, typename... OTHER_SETTINGS>
      return_t & SetAttr(const std::string & setting1, T1 && val1,
                            const std::string & setting2, T2 && val2,
                            OTHER_SETTINGS... others) {
        SetAttr(setting1, val1);                      // Set the first CSS value.
        return SetAttr(setting2, val2, others...);    // Recurse to the others.
      }

      /// Allow multiple CSS settings to be provided as a single object.
      /// (still go through DoCSS given need for virtual re-routing.)
      return_t & SetCSS(const Style & in_style) {
        emp_assert(info != nullptr);
        for (const auto & s : in_style.GetMap()) {
          DoCSS(s.first, s.second);
        }
        return (return_t &) *this;
      }

      /// Allow multiple Attr settings to be provided as a single object.
      /// (still go through DoAttr given need for virtual re-routing.)
      return_t & SetAttr(const Attributes & in_attr) {
        emp_assert(info != nullptr);
        for (const auto & a : in_attr.GetMap()) {
          DoAttr(a.first, a.second);
        }
        return (return_t &) *this;
      }

      /// Provide an event and a function that will be called when that event is triggered.
      /// In this case, the function as no arguments.
      return_t & On(const std::string & event_name, const std::function<void()> & fun) {
        emp_assert(info != nullptr);
        size_t fun_id = JSWrap(fun);
        DoListen(event_name, fun_id);
        return (return_t &) *this;
      }

      /// Provide an event and a function that will be called when that event is triggered.
      /// In this case, the function takes a mouse event as an argument, with full info about mouse.
      return_t & On(const std::string & event_name,
                    const std::function<void(MouseEvent evt)> & fun) {
        emp_assert(info != nullptr);
        size_t fun_id = JSWrap(fun);
        DoListen(event_name, fun_id);
        return (return_t &) *this;
      }

      /// Provide an event and a function that will be called when that event is triggered.
      /// In this case, the function takes two doubles which will be filled in with mouse coordinates.
      return_t & On(const std::string & event_name,
                    const std::function<void(double,double)> & fun) {
        emp_assert(info != nullptr);
        auto fun_cb = [this, fun](MouseEvent evt){
          double x = evt.clientX - GetXPos();
          double y = evt.clientY - GetYPos();
          fun(x,y);
        };
        size_t fun_id = JSWrap(fun_cb);
        DoListen(event_name, fun_id);
        return (return_t &) *this;
      }

      /// Provide a function to be called when the window is resized.
      template <typename T> return_t & OnResize(T && arg) { return On("resize", arg); }

      /// Provide a function to be called when the mouse button is clicked in this Widget.
      template <typename T> return_t & OnClick(T && arg) { return On("click", arg); }

      /// Provide a function to be called when the mouse button is double clicked in this Widget.
      template <typename T> return_t & OnDoubleClick(T && arg) { return On("dblclick", arg); }

      /// Provide a function to be called when the mouse button is pushed down in this Widget.
      template <typename T> return_t & OnMouseDown(T && arg) { return On("mousedown", arg); }

      /// Provide a function to be called when the mouse button is released in this Widget.
      template <typename T> return_t & OnMouseUp(T && arg) { return On("mouseup", arg); }

      /// Provide a function to be called whenever the mouse moves in this Widget.
      template <typename T> return_t & OnMouseMove(T && arg) { return On("mousemove", arg); }

      /// Provide a function to be called whenever the mouse leaves the Widget.
      template <typename T> return_t & OnMouseOut(T && arg) { return On("mouseout", arg); }

      /// Provide a function to be called whenever the mouse moves over the Widget.
      template <typename T> return_t & OnMouseOver(T && arg) { return On("mouseover", arg); }

      /// Provide a function to be called whenever the mouse wheel moves in this Widget.
      template <typename T> return_t & OnMouseWheel(T && arg) { return On("mousewheel", arg); }

      /// Provide a function to be called whenever a key is pressed down in this Widget.
      template <typename T> return_t & OnKeydown(T && arg) { return On("keydown", arg); }

      /// Provide a function to be called whenever a key is pressed down and released in this Widget.
      template <typename T> return_t & OnKeypress(T && arg) { return On("keypress", arg); }

      /// Provide a function to be called whenever a key is pressed released in this Widget.
      template <typename T> return_t & OnKeyup(T && arg) { return On("keyup", arg); }

      /// Provide a function to be called whenever text is copied in this Widget.
      template <typename T> return_t & OnCopy(T && arg) { return On("copy", arg); }

      /// Provide a function to be called whenever text is cut in this Widget.
      template <typename T> return_t & OnCut(T && arg) { return On("cut", arg); }

      /// Provide a function to be called whenever text is pasted in this Widget.
      template <typename T> return_t & OnPaste(T && arg) { return On("paste", arg); }


      /// Update the width of this Widget.
      /// @param unit defaults to pixels ("px"), but can also be a measured distance (e.g, "inches") or a percentage("%")
      return_t & SetWidth(double w, const std::string & unit="px") {
        return SetCSS("width", emp::to_string(w, unit) );
      }

      /// Update the height of this Widget.
      /// @param unit defaults to pixels ("px"), but can also be a measured distance (e.g, "inches") or a percentage("%")
      return_t & SetHeight(double h, const std::string & unit="px") {
        return SetCSS("height", emp::to_string(h, unit) );
      }

      /// Update the size (width and height) of this widget.
      /// @param unit defaults to pixels ("px"), but can also be a measured distance (e.g, "inches") or a percentage("%")
      return_t & SetSize(double w, double h, const std::string & unit="px") {
        SetWidth(w, unit); return SetHeight(h, unit);
      }

      /// Move this widget to the center of its container.
      return_t & Center() { return SetCSS("margin", "auto"); }

      /// Set the x-y position of this widget within its container.
      return_t & SetPosition(int x, int y, const std::string & unit="px",
                             const std::string & pos_type="absolute",
                             const std::string & x_anchor="left",
                             const std::string & y_anchor="top") {
        return SetCSS("position", pos_type,
                      x_anchor, emp::to_string(x, unit),
                      y_anchor, emp::to_string(y, unit));
      }

      /// Set the x-y position of this Widget within its container, using the TOP-RIGHT as an anchor.
      return_t & SetPositionRT(int x, int y, const std::string & unit="px")
        { return SetPosition(x, y, unit, "absolute", "right", "top"); }

      /// Set the x-y position of this Widget within its container, using the BOTTOM-RIGHT as an anchor.
      return_t & SetPositionRB(int x, int y, const std::string & unit="px")
        { return SetPosition(x, y, unit, "absolute", "right", "bottom"); }

      /// Set the x-y position of this Widget within its container, using the BOTTOM-LEFT as an anchor.
      return_t & SetPositionLB(int x, int y, const std::string & unit="px")
        { return SetPosition(x, y, unit, "absolute", "left", "bottom"); }

      /// Set the x-y position of this Widget, fixed within the browser window.
      return_t & SetPositionFixed(int x, int y, const std::string & unit="px")
        { return SetPosition(x, y, unit, "fixed", "left", "top"); }

      /// Set the x-y position of the top-right corner this Widget, fixed within the browser window.
      return_t & SetPositionFixedRT(int x, int y, const std::string & unit="px")
        { return SetPosition(x, y, unit, "fixed", "right", "top"); }

      /// Set the x-y position of the bottom-right corner this Widget, fixed within the browser window.
      return_t & SetPositionFixedRB(int x, int y, const std::string & unit="px")
        { return SetPosition(x, y, unit, "fixed", "right", "bottom"); }

      /// Set the x-y position of the bottom-left corner this Widget, fixed within the browser window.
      return_t & SetPositionFixedLB(int x, int y, const std::string & unit="px")
        { return SetPosition(x, y, unit, "fixed", "left", "bottom"); }


      /// Set this Widget to float appropriately within its containter.
      return_t & SetFloat(const std::string & f="left") { return SetCSS("float", f); }

      /// Setup how this Widget should handle overflow.
      return_t & SetOverflow(const std::string & o="auto") { return SetCSS("overflow", o); }

      /// Setup how this Widget to always have scrollbars.
      return_t & SetScroll() { return SetCSS("overflow", "scroll"); }

      /// Setup how this Widget to have scrollbars if needed for overflow.
      return_t & SetScrollAuto() { return SetCSS("overflow", "auto"); }

      /// Setup how this Widget to be user-resizable.
      return_t & SetResizable() { return SetCSS("resize", "both"); }

      /// Setup how this Widget for the x only to be user-resizable.
      return_t & SetResizableX() { return SetCSS("resize", "horizontal"); }

      /// Setup how this Widget for the y only to be user-resizable.
      return_t & SetResizableY() { return SetCSS("resize", "vertical"); }

      /// Setup how this Widget to NOT be resizable.
      return_t & SetResizableOff() { return SetCSS("resize", "none"); }

      /// Setup the Font to be used in this Widget.
      return_t & SetFont(const std::string & font) { return SetCSS("font-family", font); }

      /// Setup the size of the Font to be used in this Widget.
      return_t & SetFontSize(int s) { return SetCSS("font-size", emp::to_string(s, "px")); }

      /// Setup the size of the Font to be used in this Widget in units of % of viewport width.
      return_t & SetFontSizeVW(double s) { return SetCSS("font-size", emp::to_string(s, "vw")); }

      /// Align text to be centered.
      return_t & SetCenterText() { return SetCSS("text-align", "center"); }

      /// Set the background color of this Widget.
      return_t & SetBackground(const std::string & v) { return SetCSS("background-color", v); }

      /// Set the foreground color of this Widget.
      return_t & SetColor(const std::string & v) { return SetCSS("color", v); }

      /// Set the opacity level of this Widget.
      return_t & SetOpacity(double v) { return SetCSS("opacity", v); }

      /// Set information about the Widget board.
      return_t & SetBorder(const std::string & border_info) {
        return SetCSS("border", border_info);
      }

      /// The the number of pixels (or alternate unit) for the padding around cells (used with Tables)
      return_t & SetPadding(double p, const std::string & unit="px") {
        return SetCSS("padding", emp::to_string(p, unit));
      }
    };

  }

}
}


#endif