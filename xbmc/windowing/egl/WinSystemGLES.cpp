/*
 *      Copyright (C) 2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#include "system.h"

#ifdef HAS_EGLGLES

#include "WinSystemGLES.h"
#include "filesystem/SpecialProtocol.h"
#include "settings/Settings.h"
#include "guilib/Texture.h"
#include "utils/log.h"
#include "WinBindingEGL.h"

#include <vector>
#include "interface/vmcs_host/vc_dispmanx.h"
#include "interface/vchiq_arm/vchiq_if.h"

////////////////////////////////////////////////////////////////////////////////////////////
CWinSystemGLES::CWinSystemGLES() : CWinSystemBase()
{
  m_window = NULL;
  m_eglBinding = new CWinBindingEGL();
  // m_vendorBindings = new CDispmanx();
  m_eWindowSystem = WINDOW_SYSTEM_EGL;
}

CWinSystemGLES::~CWinSystemGLES()
{
  DestroyWindowSystem();
  delete m_eglBinding;
}

bool CWinSystemGLES::InitWindowSystem()
{
  m_display = EGL_DEFAULT_DISPLAY;
  m_window  = (EGL_DISPMANX_WINDOW_T*)calloc(1, sizeof(EGL_DISPMANX_WINDOW_T));

  // vchiq/vc_dispmanx specific inits, move out later.
  static VCHI_INSTANCE_T vchiq_instance;
  static VCHI_CONNECTION_T *vchi_connection;

  vcos_init();
  if (vchi_initialise(&vchiq_instance) != VCHIQ_SUCCESS)
  {
    CLog::Log(LOGERROR, "CWinSystemGLES::InitWindowSystem: failed to open vchiq instance");
    return false;
  }
  //create a vchi connection
  if (vchi_connect(NULL, 0, vchiq_instance) != 0)
  {
    CLog::Log(LOGERROR, "CWinSystemGLES::InitWindowSystem: VCHI connection failed");
    return false;
  }

  vc_vchi_dispmanx_init(vchiq_instance, &vchi_connection, 1);

  DISPMANX_DISPLAY_HANDLE_T dispman_display = vc_dispmanx_display_open(0);
  DISPMANX_UPDATE_HANDLE_T  dispman_update  = vc_dispmanx_update_start(0);
     
  DISPMANX_MODEINFO_T mode_info;
  memset(&mode_info, 0x0, sizeof(DISPMANX_MODEINFO_T));
  vc_dispmanx_display_get_info(dispman_display, &mode_info);

  m_fb_width  = mode_info.width;
  m_fb_height = mode_info.height;
  m_fb_bpp    = 8;

  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;
  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = m_fb_width;
  dst_rect.height = m_fb_height;

  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = m_fb_width << 16;
  src_rect.height = m_fb_height << 16;        

  DISPMANX_ELEMENT_HANDLE_T dispman_element;
  dispman_element = vc_dispmanx_element_add(dispman_update,
    dispman_display,
    0,                              // layer
    &dst_rect,
    (DISPMANX_RESOURCE_HANDLE_T)0,  // src
    &src_rect,
    DISPMANX_PROTECTION_NONE,
    (VC_DISPMANX_ALPHA_T*)0,        // alpha
    (DISPMANX_CLAMP_T*)0,           // clamp
    (DISPMANX_TRANSFORM_T)0);       // transform
    
  m_window->element = dispman_element;
  m_window->width   = m_fb_width;
  m_window->height  = m_fb_height;
  vc_dispmanx_update_submit_sync(dispman_update);

  if (!CWinSystemBase::InitWindowSystem())
    return false;

  CLog::Log(LOGDEBUG, "Video mode: %dx%d with %d bits per pixel.",
    m_fb_width, m_fb_height, m_fb_bpp);

  return true;
}

bool CWinSystemGLES::DestroyWindowSystem()
{
  free(m_window);
  m_window = NULL;

  return true;
}

bool CWinSystemGLES::CreateNewWindow(const CStdString& name, bool fullScreen, RESOLUTION_INFO& res, PHANDLE_EVENT_FUNC userFunction)
{
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;

  if (!m_eglBinding->CreateWindow((EGLNativeDisplayType)m_display, (EGLNativeWindowType)m_window))
    return false;

  UpdateDesktopResolution(g_settings.m_ResInfo[RES_DESKTOP], 0, m_nWidth, m_nHeight, 0.0);

  m_bWindowCreated = true;

  return true;
}

bool CWinSystemGLES::DestroyWindow()
{
  if (!m_eglBinding->DestroyWindow())
    return false;

  m_bWindowCreated = false;

  return true;
}

bool CWinSystemGLES::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  CRenderSystemGLES::ResetRenderSystem(newWidth, newHeight, true, 0);

  return true;
}

bool CWinSystemGLES::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  CLog::Log(LOGDEBUG, "CWinSystemDFB::SetFullScreen");
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;

  m_eglBinding->ReleaseSurface();
  CreateNewWindow("", fullScreen, res, NULL);

  CRenderSystemGLES::ResetRenderSystem(res.iWidth, res.iHeight, true, 0);

  return true;
}

void CWinSystemGLES::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  // here is where we would probe the avaliable display resolutions from display.
  // hard code now to what we get from vc_dispmanx_display_get_info
  int w = m_fb_width;
  int h = m_fb_height;
  UpdateDesktopResolution(g_settings.m_ResInfo[RES_DESKTOP], 0, w, h, 0.0);
}

bool CWinSystemGLES::IsExtSupported(const char* extension)
{
  if(strncmp(extension, "EGL_", 4) != 0)
    return CRenderSystemGLES::IsExtSupported(extension);

  return m_eglBinding->IsExtSupported(extension);
}

bool CWinSystemGLES::PresentRenderImpl(const CDirtyRegionList &dirty)
{
  m_eglBinding->SwapBuffers();

  return true;
}

void CWinSystemGLES::SetVSyncImpl(bool enable)
{
  m_iVSyncMode = enable ? 10 : 0;
  if (m_eglBinding->SetVSync(enable) == FALSE)
    CLog::Log(LOGERROR, "CWinSystemDFB::SetVSyncImpl: Could not set egl vsync");
}

void CWinSystemGLES::ShowOSMouse(bool show)
{
}

void CWinSystemGLES::NotifyAppActiveChange(bool bActivated)
{
}

bool CWinSystemGLES::Minimize()
{
  Hide();
  return true;
}

bool CWinSystemGLES::Restore()
{
  Show(true);
  return false;
}

bool CWinSystemGLES::Hide()
{
  return true;
}

bool CWinSystemGLES::Show(bool raise)
{
  return true;
}

EGLContext CWinSystemGLES::GetEGLContext() const
{
  return m_eglBinding->GetContext();
}

EGLDisplay CWinSystemGLES::GetEGLDisplay() const
{
  return m_eglBinding->GetDisplay();
}

#endif
