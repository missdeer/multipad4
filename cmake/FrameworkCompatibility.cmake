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
      # CPtrFrame is a deque element and must have non-destructive copy
      # semantics. Modern deque middle insertion may copy a saved value more
      # than once; mutable auto_ptr steals it and leaves a null frame behind.
      # Match the upstream USE_BOOST branch's shared ownership with the STL.
      string(REPLACE "mutable std::auto_ptr<T> m_ptr;" "std::shared_ptr<T> m_ptr;" contents "${contents}")
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
      # Desktop XOR feedback is unreliable under DWM. Move floating windows
      # during tracking, while retaining the framework's dock-target detection.
      # Mouse messages are relative to the moving window, so keep the grab
      # offset in screen coordinates rather than relative to its initial origin.
      string(REPLACE "m_offset.cx=m_dockHdr.rect.left-pt.x;\n\t\t\tm_offset.cy=m_dockHdr.rect.top-pt.y;"
        "m_liveMove=(::GetWindowLongPtr(m_dockHdr.hdr.hWnd,GWL_STYLE)&WS_CHILD)==0;\n\t\t\t::GetWindowRect(m_dockHdr.hdr.hWnd,&m_originalRect);\n\t\t\tCPoint screenPoint(pt);\n\t\t\t::ClientToScreen(m_dockHdr.hdr.hWnd,&screenPoint);\n\t\t\tm_offset.cx=m_dockHdr.rect.left-screenPoint.x;\n\t\t\tm_offset.cy=m_dockHdr.rect.top-screenPoint.y;" contents "${contents}")
      string(REPLACE "void DrawGhostRect(CDC& dc,RECT* pRect)\n\t\t{"
        "void DrawGhostRect(CDC& dc,RECT* pRect)\n\t\t{\n\t\t\tif(m_liveMove) return;" contents "${contents}")
      string(REPLACE "void EndDrag(bool /*bCanceled*/)\n\t\t{\n\t\t\tCleanGhostRect(m_dc,&m_dockHdr.rect);"
        "void EndDrag(bool bCanceled)\n\t\t{\n\t\t\tif(m_liveMove && bCanceled)\n\t\t\t\t::SetWindowPos(m_dockHdr.hdr.hWnd,NULL,m_originalRect.left,m_originalRect.top,m_originalRect.Width(),m_originalRect.Height(),SWP_NOZORDER|SWP_NOACTIVATE);\n\t\t\tCleanGhostRect(m_dc,&m_dockHdr.rect);" contents "${contents}")
      string(REPLACE "m_dockHdr.rect.left=x;\n\t\t\tm_dockHdr.rect.top=y;"
        "CPoint screenPoint(x,y);\n\t\t\t::ClientToScreen(m_dockHdr.hdr.hWnd,&screenPoint);\n\t\t\tm_dockHdr.rect.left=x;\n\t\t\tm_dockHdr.rect.top=y;" contents "${contents}")
      string(REPLACE "m_dockHdr.rect.left=x+m_offset.cx;" "m_dockHdr.rect.left=screenPoint.x+m_offset.cx;" contents "${contents}")
      string(REPLACE "m_dockHdr.rect.top=y+m_offset.cy;" "m_dockHdr.rect.top=screenPoint.y+m_offset.cy;" contents "${contents}")
      string(REPLACE "DrawGhostRect(m_dc,&m_dockHdr.rect);\n\t\t}\n\t\tbool ProcessWindowMessage"
        "if(m_liveMove)\n\t\t\t\t::SetWindowPos(m_dockHdr.hdr.hWnd,NULL,screenPoint.x+m_offset.cx,screenPoint.y+m_offset.cy,m_size.cx,m_size.cy,SWP_NOZORDER|SWP_NOACTIVATE);\n\t\t\tDrawGhostRect(m_dc,&m_dockHdr.rect);\n\t\t}\n\t\tbool ProcessWindowMessage" contents "${contents}")
      string(REPLACE "const CDocker&\tm_docker;" "bool m_liveMove;\n\t\tCRect m_originalRect;\n\t\tconst CDocker&\tm_docker;" contents "${contents}")
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
