/** @file
  Debug Print Error Level library instance that provide compatibility with the 
  "err" shell command.  This includes support for the Debug Mask Protocol
  supports for global debug print error level mask stored in an EFI Variable.
  This library instance only support DXE Phase modules.

  Copyright (c) 2011, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiDxe.h>

#include <Library/DebugPrintErrorLevelLib.h>
#include <Library/PcdLib.h>

#include <Guid/DebugMask.h>

///
/// Debug Mask Protocol function prototypes
///
EFI_STATUS
EFIAPI
GetDebugMask (
  IN EFI_DEBUG_MASK_PROTOCOL  *This,             
  IN OUT UINTN                *CurrentDebugMask  
  );

EFI_STATUS
EFIAPI
SetDebugMask (
  IN EFI_DEBUG_MASK_PROTOCOL  *This,
  IN UINTN                    NewDebugMask
  );

///
/// Debug Mask Protocol instance
///
EFI_DEBUG_MASK_PROTOCOL  mDebugMaskProtocol = {
  EFI_DEBUG_MASK_REVISION,
  GetDebugMask,
  SetDebugMask
};

///
/// Global variable that is set to TRUE after the first attempt is made to 
/// retrieve the global error level mask through the EFI Varibale Services.
/// This variable prevents the EFI Variable Services from being called fort
/// every DEBUG() macro.
///
BOOLEAN           mGlobalErrorLevelInitialized = FALSE;

///
/// Global variable that contains the current debug error level mask for the
/// module that is using this library instance.  This variable is initially
/// set to the PcdDebugPrintErrorLevel value.  If the EFI Variable exists that
/// contains the global debug print error level mask, then that override the
/// PcdDebugPrintErrorLevel value.  If the Debug Mask Protocol SetDebugMask()
/// service is called, then that overrides bit the PcdDebugPrintErrorLevel and
/// the EFI Variable setting.
///
UINT32            mDebugPrintErrorLevel        = 0;

///
/// Global variable that is used to cache a pointer to the EFI System Table
/// that is required to access the EFI Variable Services to get and set
/// the global debug print error level mask value.  The UefiBootServicesTableLib
/// is not used to prevent a circular dependency between these libraries.
///
EFI_SYSTEM_TABLE  *mST                         = NULL;

/**
  The constructor function caches the PCI Express Base Address and creates a 
  Set Virtual Address Map event to convert physical address to virtual addresses.
  
  @param  ImageHandle   The firmware allocated handle for the EFI image.
  @param  SystemTable   A pointer to the EFI System Table.
  
  @retval EFI_SUCCESS   The constructor completed successfully.
  @retval Other value   The constructor did not complete successfully.

**/
EFI_STATUS
EFIAPI
DxeDebugPrintErrorLevelLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  //
  // Initialize the error level mask from PCD setting.
  //
  mDebugPrintErrorLevel = PcdGet32 (PcdDebugPrintErrorLevel);
  
  //
  // Install Debug Mask Protocol onto ImageHandle
  //  
  mST = SystemTable;
  Status = SystemTable->BootServices->InstallMultipleProtocolInterfaces (
                                        &ImageHandle,
                                        &gEfiDebugMaskProtocolGuid, &mDebugMaskProtocol,
                                        NULL
                                        );

  //
  // Attempt to retrieve the global debug print error level mask from the EFI Variable
  // If the EFI Variable can not be accessed when this module's library constructors are
  // executed, then the EFI Variable access will be reattempted on every DEBUG() print
  // from this module until the EFI Variable services are available.
  //
  GetDebugPrintErrorLevel ();
  
  return Status;
}

/**
  The destructor function frees any allocated buffers and closes the Set Virtual 
  Address Map event.
  
  @param  ImageHandle   The firmware allocated handle for the EFI image.
  @param  SystemTable   A pointer to the EFI System Table.
  
  @retval EFI_SUCCESS   The destructor completed successfully.
  @retval Other value   The destructor did not complete successfully.

**/
EFI_STATUS
EFIAPI
DxeDebugPrintErrorLevelLibDestructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  //
  // Uninstall the Debug Mask Protocol from ImageHandle
  //  
  return SystemTable->BootServices->UninstallMultipleProtocolInterfaces (
                                      ImageHandle,
                                      &gEfiDebugMaskProtocolGuid, &mDebugMaskProtocol,
                                      NULL
                                      );
}

/**
  Returns the debug print error level mask for the current module.

  @return  Debug print error level mask for the current module.

**/
UINT32
EFIAPI
GetDebugPrintErrorLevel (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_TPL     CurrentTpl;
  UINTN       Size;
  UINTN       GlobalErrorLevel;

  //
  // If the constructor has not been executed yet, then just return the PCD value.
  // This case should only occur if debug print is generated by a library
  // constructor for this module
  //
  if (mST == NULL) {
    return PcdGet32 (PcdDebugPrintErrorLevel);
  }

  //
  // Check to see if an attempt has been mde to retrieve the global debug print 
  // error level mask.  Since this libtrary instance stores the global debug print
  // error level mask in an EFI Variable, the EFI Variable should only be accessed
  // once to reduce the overhead of reading the EFI Variable on every debug print
  //  
  if (!mGlobalErrorLevelInitialized) {
    //
    // Make sure the TPL Level is low enough for EFI Variable Services to be called
    //
    CurrentTpl = mST->BootServices->RaiseTPL (TPL_HIGH_LEVEL);
    mST->BootServices->RestoreTPL (CurrentTpl);
    if (CurrentTpl <= TPL_CALLBACK) {
      //
      // Attempt to retrieve the global debug print error level mask from the 
      // EFI Variable
      //
      Size = sizeof (GlobalErrorLevel);
      Status = mST->RuntimeServices->GetVariable (
                                       DEBUG_MASK_VARIABLE_NAME, 
                                       &gEfiGenericVariableGuid, 
                                       NULL, 
                                       &Size, 
                                       &GlobalErrorLevel
                                       );
      if (Status != EFI_NOT_AVAILABLE_YET) {
        //
        // If EFI Variable Services are available, then set a flag so the EFI
        // Variable will not be read again by this module.
        //
        mGlobalErrorLevelInitialized = TRUE;
        if (!EFI_ERROR (Status)) {
          //
          // If the EFI Varible exists, then set this module's module's mask to
          // the global debug print error level mask value.
          //
          mDebugPrintErrorLevel = (UINT32)GlobalErrorLevel;
        }
      }
    }
  }

  //
  // Return the current mask value for this module.
  //
  return mDebugPrintErrorLevel;
}

/**
  Sets the global debug print error level mask fpr the entire platform.

  @retval  TRUE   The debug print error level mask was sucessfully set.
  @retval  FALSE  The debug print error level mask could not be set.

**/
BOOLEAN
EFIAPI
SetDebugPrintErrorLevel (
  UINT32  ErrorLevel
  )
{
  EFI_STATUS  Status;
  EFI_TPL     CurrentTpl;
  UINTN       Size;
  UINTN       GlobalErrorLevel;

  //
  // Make sure the constructor has been executed
  //
  if (mST != NULL) {
    //
    // Make sure the TPL Level is low enough for EFI Variable Services
    //
    CurrentTpl = mST->BootServices->RaiseTPL (TPL_HIGH_LEVEL);
    mST->BootServices->RestoreTPL (CurrentTpl);
    if (CurrentTpl <= TPL_CALLBACK) {
      //
      // Attempt to store the global debug print error level mask in an EFI Variable
      //
      GlobalErrorLevel = (UINTN)ErrorLevel;
      Size = sizeof (GlobalErrorLevel);
      Status = mST->RuntimeServices->SetVariable (
                                       DEBUG_MASK_VARIABLE_NAME, 
                                       &gEfiGenericVariableGuid, 
                                       (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS),
                                       Size,
                                       &GlobalErrorLevel
                                       );
      if (!EFI_ERROR (Status)) {
        //
        // If the EFI Variable was updated, then update the mask value for this 
        // module and return TRUE.
        //
        mGlobalErrorLevelInitialized = TRUE;    
        mDebugPrintErrorLevel = ErrorLevel;
        return TRUE;
      }
    }
  }
  //
  // Return FALSE since the EFI Variable could not be updated.
  //
  return FALSE;
}

/**
  Retrieves the current debug print error level mask for a module are returns
  it in CurrentDebugMask.

  @param  This              The protocol instance pointer.
  @param  CurrentDebugMask  Pointer to the debug print error level mask that 
                            is returned.

  @retval EFI_SUCCESS            The current debug print error level mask was
                                 returned in CurrentDebugMask.
  @retval EFI_INVALID_PARAMETER  CurrentDebugMask is NULL.
  @retval EFI_DEVICE_ERROR       The current debug print error level mask could
                                 not be retrieved.

**/
EFI_STATUS
EFIAPI
GetDebugMask (
  IN EFI_DEBUG_MASK_PROTOCOL  *This,             
  IN OUT UINTN                *CurrentDebugMask  
  )
{
  if (CurrentDebugMask == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  //
  // Retrieve the current debug mask from mDebugPrintErrorLevel
  //
  *CurrentDebugMask = (UINTN)mDebugPrintErrorLevel;
  return EFI_SUCCESS;
}

/**
  Sets the current debug print error level mask for a module to the value
  specified by NewDebugMask.

  @param  This          The protocol instance pointer.
  @param  NewDebugMask  The new debug print error level mask for this module.

  @retval EFI_SUCCESS            The current debug print error level mask was
                                 set to the value specified by NewDebugMask.
  @retval EFI_DEVICE_ERROR       The current debug print error level mask could
                                 not be set to the value specified by NewDebugMask.

**/
EFI_STATUS
EFIAPI
SetDebugMask (
  IN EFI_DEBUG_MASK_PROTOCOL  *This,
  IN UINTN                    NewDebugMask
  )
{
  //
  // Store the new debug mask into mDebugPrintErrorLevel
  //
  mDebugPrintErrorLevel = (UINT32)NewDebugMask;
  return EFI_SUCCESS;
}
