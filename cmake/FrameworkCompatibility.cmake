# Preserve unmodified upstream snapshots and apply narrow MSVC/oneAPI compatibility
# adaptations only to build-directory copies. No substitute framework is used.
set(FRAMEWORK_COMPAT_DIR "${CMAKE_BINARY_DIR}/framework-compat")
foreach(framework TabbingFramework DockingFramework)
  file(GLOB headers CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/3rdparty/${framework}/*.h" "${CMAKE_SOURCE_DIR}/3rdparty/${framework}/*.cpp")
  foreach(header IN LISTS headers)
    get_filename_component(name "${header}" NAME)
    file(READ "${header}" contents)
    if(name STREQUAL "TabbedMDI.h")
      # MSVC permits the later client declaration to satisfy the frame's
      # default argument; Clang-based oneAPI requires it to be declared first.
      set(mdi_client_template "template< class TTabCtrl = CDotNetTabCtrl<CTabViewTabItem>, class TTabOwner = CMDITabOwner<TTabCtrl> >")
      string(REPLACE "${mdi_client_template}" "template< class TTabCtrl, class TTabOwner >" contents "${contents}")
      string(REPLACE "template <\n\tclass T,\n\tclass TClient ="
        "template<class TTabCtrl> class CMDITabOwner;\n${mdi_client_template}\nclass CTabbedMDIClient;\n\ntemplate <\n\tclass T,\n\tclass TClient =" contents "${contents}")
      string(REPLACE "typedef typename TClient TClient;" "using ClientType = TClient;" contents "${contents}")
      string(REPLACE "CTabbedMDIFrameWindowImpl< T, TBase, TWinTraits >" "CTabbedMDIFrameWindowImpl< T, TClient, TBase, TWinTraits >" contents "${contents}")
      # Layout must reserve the tab strip even while its frame is hidden.
      # IsWindowVisible includes ancestor visibility and otherwise collapses
      # the strip during hidden startup, tests, and minimize-to-tray layout.
      string(REPLACE "(m_MdiTabOwner.IsWindowVisible()) ? m_MdiTabOwner.GetTabAreaHeight() : 0"
        "((m_MdiTabOwner.GetStyle() & WS_VISIBLE) != 0) ? m_MdiTabOwner.GetTabAreaHeight() : 0" contents "${contents}")
    endif()
    if(name STREQUAL "WndFrmPkg.h")
      string(REPLACE "virtual void Move()=0;" "virtual ~CSplitterMoveTrackerBase() = default;\n\t\tvirtual void Move()=0;" contents "${contents}")
      string(REPLACE "static distance min_distance" "static typename T::distance min_distance" contents "${contents}")
      string(REPLACE "const distance dist=TMinDist;" "const typename T::distance dist=TMinDist;" contents "${contents}")
      string(REPLACE "bool AcceptDock(const_iterator i," "bool AcceptDock(typename baseClass::const_iterator i," contents "${contents}")
      # The nested splitter must resolve its inherited orientation method,
      # rather than the enclosing package's similarly named method.
      string(REPLACE "if(IsHorizontal())" "if(this->IsHorizontal())" contents "${contents}")
      string(REPLACE "CFrame::CCmp(" "typename CFrame::CCmp(" contents "${contents}")
    endif()
    if(name STREQUAL "ssec.h")
      string(REPLACE "TTraits=spraits<>" "TTraits=spraits<T, long>" contents "${contents}")
      string(REPLACE "std::bind2nd(std::plus<position>(),n)" "[n](const auto& value) { return value + n; }" contents "${contents}")
    endif()
    if(name STREQUAL "DockingWindow.h")
      # Disambiguate the dependent window handle from a function declaration.
      string(REPLACE "CWindowDC dc(m_hWnd);" "WTL::CWindowDC dc(this->m_hWnd);" contents "${contents}")
    endif()
    if(name STREQUAL "TabbedFrame.h" OR name STREQUAL "TabbedMDI.h")
      string(REPLACE "TTabCtrl::TItem" "typename TTabCtrl::TItem" contents "${contents}")
    endif()
    if(name STREQUAL "SimpleSplitterBar.h")
      # WTL creates a new brush but returns a non-owning handle wrapper.
      # Adopt the raw handle explicitly, preserving CBrush's cleanup without
      # requiring two implicit user-defined conversions in the initializer.
      string(REPLACE "CBrush brush = CDCHandle::GetHalftoneBrush();" "CBrush brush(CDCHandle::GetHalftoneBrush().m_hBrush);" contents "${contents}")
    endif()
    string(REPLACE "CClientDC dc(m_hWnd);" "CClientDC dc(this->m_hWnd);" contents "${contents}")
    if(name STREQUAL "DockImpl.cpp")
      string(REPLACE "sizeof(sEllipsis)-1" "static_cast<int>(_tcslen(sEllipsis))" contents "${contents}")
    endif()
    file(CONFIGURE OUTPUT "${FRAMEWORK_COMPAT_DIR}/${framework}/${name}" CONTENT "${contents}" @ONLY)
  endforeach()
endforeach()
