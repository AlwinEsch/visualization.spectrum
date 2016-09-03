#ifndef OPENGL_SPECTRUM_H
#define OPENGL_SPECTRUM_H

#include <kodi/api2/Addon.hpp>

class CAddonVisualizationSpectrum : public CAddon
{
public:
  CAddonVisualizationSpectrum() { }

  virtual eAddonStatus Create() override;
  virtual void Destroy() override;
  virtual eAddonStatus CreateInstance(eInstanceType instanceType, KODI_HANDLE& addonInstance, KODI_HANDLE kodiInstance) override;
  virtual void DestroyInstance(KODI_HANDLE addonInstance) override;
  virtual bool HasSettings() override { return true; }
  virtual eAddonStatus SetSetting(std::string& settingName, const void *settingValue) override;
};

#endif
