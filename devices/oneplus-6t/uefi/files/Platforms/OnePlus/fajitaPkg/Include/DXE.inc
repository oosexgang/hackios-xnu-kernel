  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

  INF Binaries/enchilada/QcomPkg/Drivers/TzDxe/TzDxeLA.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf

  INF Binaries/enchilada/QcomPkg/Drivers/EnvDxe/EnvDxe.inf

  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF Binaries/enchilada/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/SmemDxe/SmemDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/enchilada/QcomPkg/Drivers/CPRDxe/CPRDxe.inf
!endif
  INF Binaries/enchilada/QcomPkg/Drivers/PdcDxe/PdcDxe.inf

  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  INF Binaries/enchilada/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/GpiDxe/GpiDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/SdccDxe/SdccDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/UFSDxe/UFSDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/enchilada/QcomPkg/Drivers/DisplayDxe/DisplayDxe.inf
  INF QcomPkg/Drivers/DisplayReEnablerDxe/DisplayReEnablerDxe.inf
!else
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif

  INF Binaries/enchilada/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/ButtonsDxe/ButtonsDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
  INF Binaries/enchilada/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.inf
  INF Binaries/enchilada/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/AdcDxe/AdcDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/TsensDxe/TsensDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/LimitsDxe/LimitsDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/DDRInfoDxe/DDRInfoDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
  INF Binaries/generic/QcomPkg/Drivers/XhciPciEmulationDxe/XhciPciEmulationDxe.inf
  INF Binaries/generic/QcomPkg/Drivers/XhciDxe/XhciDxe.inf
  
  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  INF Binaries/enchilada/QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/HashDxe/HashDxe.inf
  INF Binaries/enchilada/QcomPkg/Drivers/RNGDxe/RngDxe.inf

  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
