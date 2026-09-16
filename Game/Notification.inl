static const char *ErrorToString(HRESULT code)
{
    switch (code)
    {
    // DirectDraw error codes
    case DD_OK:
        return "No error.\n";
    case DDERR_ALREADYINITIALIZED:
        return "This object is already initialized.\n";
    case DDERR_BLTFASTCANTCLIP:
        return "Return if a clipper object is attached to the source surface passed into a BltFast "
               "call.\n";
    case DDERR_CANNOTATTACHSURFACE:
        return "This surface can not be attached to the requested surface.\n";
    case DDERR_CANNOTDETACHSURFACE:
        return "This surface can not be detached from the requested surface.\n";
    case DDERR_CANTCREATEDC:
        return "Windows can not create any more DCs.\n";
    case DDERR_CANTDUPLICATE:
        return "Can't duplicate primary & 3D surfaces, or surfaces that are implicitly created.\n";
    case DDERR_CLIPPERISUSINGHWND:
        return "An attempt was made to set a cliplist for a clipper object that is already "
               "monitoring an hwnd.\n";
    case DDERR_COLORKEYNOTSET:
        return "No src color key specified for this operation.\n";
    case DDERR_CURRENTLYNOTAVAIL:
        return "Support is currently not available.\n";
    case DDERR_DIRECTDRAWALREADYCREATED:
        return "A DirectDraw object representing this driver has already been created for this "
               "process.\n";
    case DDERR_EXCEPTION:
        return "An exception was encountered while performing the requested operation.\n";
    case DDERR_EXCLUSIVEMODEALREADYSET:
        return "An attempt was made to set the cooperative level when it was already set to "
               "exclusive.\n";
    case DDERR_GENERIC:
        return "Generic failure.\n";
    case DDERR_HEIGHTALIGN:
        return "Height of rectangle provided is not a multiple of reqd alignment.\n";
    case DDERR_HWNDALREADYSET:
        return "The CooperativeLevel HWND has already been set. It can not be reset while the "
               "process has surfaces or palettes created.\n";
    case DDERR_HWNDSUBCLASSED:
        return "HWND used by DirectDraw CooperativeLevel has been subclassed, this prevents "
               "DirectDraw from restoring state.\n";
    case DDERR_IMPLICITLYCREATED:
        return "This surface can not be restored because it is an implicitly created surface.\n";
    case DDERR_INCOMPATIBLEPRIMARY:
        return "Unable to match primary surface creation request with existing primary surface.\n";
    case DDERR_INVALIDCAPS:
        return "One or more of the caps bits passed to the callback are incorrect.\n";
    case DDERR_INVALIDCLIPLIST:
        return "DirectDraw does not support the provided cliplist.\n";
    case DDERR_INVALIDDIRECTDRAWGUID:
        return "The GUID passed to the DirectDrawCreate function is not a valid DirectDraw driver "
               "identifier\nProbably you have a wrong driver\n";
    case DDERR_INVALIDMODE:
        return "DirectDraw does not support the requested mode.\n";
    case DDERR_INVALIDOBJECT:
        return "DirectDraw received a pointer that was an invalid DIRECTDRAW object.\n";
    case DDERR_INVALIDPARAMS:
        return "One or more of the parameters passed to the function are incorrect.\n";
    case DDERR_INVALIDPIXELFORMAT:
        return "The pixel format was invalid as specified.\n";
    case DDERR_INVALIDPOSITION:
        return "Returned when the position of the overlay on the destination is no longer legal "
               "for that destination.\n";
    case DDERR_INVALIDRECT:
        return "Rectangle provided was invalid.\n";
    case DDERR_LOCKEDSURFACES:
        return "Operation could not be carried out because one or more surfaces are locked.\n";
    case DDERR_NO3D:
        return "There is no 3D present.\n";
    case DDERR_NOALPHAHW:
        return "Operation could not be carried out because there is no alpha accleration hardware "
               "present or available.\n";
    case DDERR_NOBLTHW:
        return "No blitter hardware present.\n";
    case DDERR_NOCLIPLIST:
        return "No cliplist available.\n";
    case DDERR_NOCLIPPERATTACHED:
        return "No clipper object attached to surface object.\n";
    case DDERR_NOCOLORCONVHW:
        return "Operation could not be carried out because there is no color conversion hardware "
               "present or available.\n";
    case DDERR_NOCOLORKEY:
        return "Surface doesn't currently have a color key\n";
    case DDERR_NOCOLORKEYHW:
        return "Operation could not be carried out because there is no hardware support of the "
               "destination color key.\n";
    case DDERR_NOCOOPERATIVELEVELSET:
        return "Create function called without DirectDraw object method SetCooperativeLevel being "
               "called.\n";
    case DDERR_NODC:
        return "No DC was ever created for this surface.\n";
    case DDERR_NODDROPSHW:
        return "No DirectDraw ROP hardware.\n";
    case DDERR_NODIRECTDRAWHW:
        return "A hardware-only DirectDraw object creation was attempted but the driver did not "
               "support any hardware.\n";
    case DDERR_NOEMULATION:
        return "Software emulation not available.\n";
    case DDERR_NOEXCLUSIVEMODE:
        return "Operation requires the application to have exclusive mode but the application does "
               "not have exclusive mode.\n";
    case DDERR_NOFLIPHW:
        return "Flipping visible surfaces is not supported.\n";
    case DDERR_NOGDI:
        return "There is no GDI present.\n";
    case DDERR_NOHWND:
        return "Clipper notification requires an HWND or no HWND has previously been set as the "
               "CooperativeLevel HWND.\n";
    case DDERR_NOMIRRORHW:
        return "Operation could not be carried out because there is no hardware present or "
               "available.\n";
    case DDERR_NOOVERLAYDEST:
        return "Returned when GetOverlayPosition is called on an overlay that UpdateOverlay has "
               "never been called on to establish a destination.\n";
    case DDERR_NOOVERLAYHW:
        return "Operation could not be carried out because there is no overlay hardware present or "
               "available.\n";
    case DDERR_NOPALETTEATTACHED:
        return "No palette object attached to this surface.\n";
    case DDERR_NOPALETTEHW:
        return "No hardware support for 16 or 256 color palettes.\n";
    case DDERR_NORASTEROPHW:
        return "Operation could not be carried out because there is no appropriate raster op "
               "hardware present or available.\n";
    case DDERR_NOROTATIONHW:
        return "Operation could not be carried out because there is no rotation hardware present "
               "or available.\n";
    case DDERR_NOSTRETCHHW:
        return "Operation could not be carried out because there is no hardware support for "
               "stretching.\n";
    case DDERR_NOT4BITCOLOR:
        return "DirectDrawSurface is not in 4 bit color palette and the requested operation "
               "requires 4 bit color palette.\n";
    case DDERR_NOT4BITCOLORINDEX:
        return "DirectDrawSurface is not in 4 bit color index palette and the requested operation "
               "requires 4 bit color index palette.\n";
    case DDERR_NOT8BITCOLOR:
        return "DirectDrawSurface is not in 8 bit color mode and the requested operation requires "
               "8 bit color.\n";
    case DDERR_NOTAOVERLAYSURFACE:
        return "Returned when an overlay member is called for a non-overlay surface.\n";
    case DDERR_NOTEXTUREHW:
        return "Operation could not be carried out because there is no texture mapping hardware "
               "present or available.\n";
    case DDERR_NOTFLIPPABLE:
        return "An attempt has been made to flip a surface that is not flippable.\n";
    case DDERR_NOTFOUND:
        return "Requested item was not found.\n";
    case DDERR_NOTLOCKED:
        return "Surface was not locked.  An attempt to unlock a surface that was not locked at "
               "all, or by this process, has been attempted.\n";
    case DDERR_NOTPALETTIZED:
        return "The surface being used is not a palette-based surface.\n";
    case DDERR_NOVSYNCHW:
        return "Operation could not be carried out because there is no hardware support for "
               "vertical blank synchronized operations.\n";
    case DDERR_NOZBUFFERHW:
        return "Operation could not be carried out because there is no hardware support for "
               "zbuffer blitting.\n";
    case DDERR_NOZOVERLAYHW:
        return "Overlay surfaces could not be z layered based on their BltOrder because the "
               "hardware does not support z layering of overlays.\n";
    case DDERR_OUTOFCAPS:
        return "The hardware needed for the requested operation has already been allocated.\n";
    case DDERR_OUTOFMEMORY:
        return "DirectDraw does not have enough memory to perform the operation.\n";
    case DDERR_OUTOFVIDEOMEMORY:
        return "DirectDraw does not have enough memory to perform the operation.\n";
    case DDERR_OVERLAYCANTCLIP:
        return "The hardware does not support clipped overlays.\n";
    case DDERR_OVERLAYCOLORKEYONLYONEACTIVE:
        return "Can only have ony color key active at one time for overlays.\n";
    case DDERR_OVERLAYNOTVISIBLE:
        return "Returned when GetOverlayPosition is called on a hidden overlay.\n";
    case DDERR_PALETTEBUSY:
        return "Access to this palette is being refused because the palette is already locked by "
               "another thread.\n";
    case DDERR_PRIMARYSURFACEALREADYEXISTS:
        return "This process already has created a primary surface.\n";
    case DDERR_REGIONTOOSMALL:
        return "Region passed to Clipper::GetClipList is too small.\n";
    case DDERR_SURFACEALREADYATTACHED:
        return "This surface is already attached to the surface it is being attached to.\n";
    case DDERR_SURFACEALREADYDEPENDENT:
        return "This surface is already a dependency of the surface it is being made a dependency "
               "of.\n";
    case DDERR_SURFACEBUSY:
        return "Access to this surface is being refused because the surface is already locked by "
               "another thread.\n";
    case DDERR_SURFACEISOBSCURED:
        return "Access to surface refused because the surface is obscured.\n";
    case DDERR_SURFACELOST:
        return "Access to this surface is being refused because the surface memory is gone. The "
               "DirectDrawSurface object representing this surface should have Restore called on "
               "it.\n";
    case DDERR_SURFACENOTATTACHED:
        return "The requested surface is not attached.\n";
    case DDERR_TOOBIGHEIGHT:
        return "Height requested by DirectDraw is too large.\n";
    case DDERR_TOOBIGSIZE:
        return "Size requested by DirectDraw is too large, but the individual height and width are "
               "OK.\n";
    case DDERR_TOOBIGWIDTH:
        return "Width requested by DirectDraw is too large.\n";
    case DDERR_UNSUPPORTED:
        return "Action not supported.\n";
    case DDERR_UNSUPPORTEDFORMAT:
        return "FOURCC format requested is unsupported by DirectDraw.\n";
    case DDERR_UNSUPPORTEDMASK:
        return "Bitmask in the pixel format requested is unsupported by DirectDraw.\n";
    case DDERR_VERTICALBLANKINPROGRESS:
        return "Vertical blank is in progress.\n";
    case DDERR_WASSTILLDRAWING:
        return "Informs DirectDraw that the previous Blt which is transfering information to or "
               "from this Surface is incomplete.\n";
    case DDERR_WRONGMODE:
        return "This surface can not be restored because it was created in a different mode.\n";
    case DDERR_XALIGN:
        return "Rectangle provided was not horizontally aligned on required boundary.\n";

    // Direct3D error codes
    case DDERR_NONONLOCALVIDMEM:
        return "An attempt was made to allocate non-local video memory from a device that does not "
               "support non-local video memory.\n";
    case DDERR_NOMIPMAPHW:
        return "No mipmap-capable texture mapping hardware is present or available\n";
    case D3DERR_VBUF_CREATE_FAILED:
        return "The vertex buffer could not be created. This can happen when there is insufficient "
               "memory to allocate a vertex buffer\n";
    case D3DERR_VERTEXBUFFERLOCKED:
        return "The requested operation could not be completed because the vertex buffer is "
               "locked\n";
    case D3DERR_VERTEXBUFFEROPTIMIZED:
        return "The requested operation could not be completed because the vertex buffer is "
               "optimized\n";
    case D3DERR_VERTEXBUFFERUNLOCKFAILED:
        return "The vertex buffer could not be unlocked because the vertex buffer memory was "
               "overrun.\n Be sure that your application does not write beyond the size of the "
               "vertex buffer\n";
    case D3DERR_VIEWPORTDATANOTSET:
        return "The requested operation could not be completed because viewport parameters have "
               "not yet been set\n";
    case D3DERR_SURFACENOTINVIDMEM:
        return "The device could not be created because the render target surface is not located "
               "in video memory\n";
    case D3DERR_BADMAJORVERSION:
        return "D3DERR_BADMAJORVERSION\n";
    case D3DERR_BADMINORVERSION:
        return "D3DERR_BADMINORVERSION\n";
    case D3DERR_EXECUTE_LOCKED:
        return "D3DERR_EXECUTE_LOCKED\n";
    case D3DERR_EXECUTE_NOT_LOCKED:
        return "D3DERR_EXECUTE_NOT_LOCKED\n";
    case D3DERR_EXECUTE_CREATE_FAILED:
        return "D3DERR_EXECUTE_CREATE_FAILED\n";
    case D3DERR_EXECUTE_DESTROY_FAILED:
        return "D3DERR_EXECUTE_DESTROY_FAILED\n";
    case D3DERR_EXECUTE_LOCK_FAILED:
        return "D3DERR_EXECUTE_LOCK_FAILED\n";
    case D3DERR_EXECUTE_UNLOCK_FAILED:
        return "D3DERR_EXECUTE_UNLOCK_FAILED\n";
    case D3DERR_EXECUTE_FAILED:
        return "D3DERR_EXECUTE_FAILED\n";
    case D3DERR_EXECUTE_CLIPPED_FAILED:
        return "D3DERR_EXECUTE_CLIPPED_FAILED\n";
    case D3DERR_TEXTURE_NO_SUPPORT:
        return "D3DERR_TEXTURE_NO_SUPPORT\n";
    case D3DERR_TEXTURE_NOT_LOCKED:
        return "D3DERR_TEXTURE_NOT_LOCKED\n";
    case D3DERR_TEXTURE_LOCKED:
        return "D3DERR_TEXTURELOCKED\n";
    case D3DERR_TEXTURE_CREATE_FAILED:
        return "D3DERR_TEXTURE_CREATE_FAILED\n";
    case D3DERR_TEXTURE_DESTROY_FAILED:
        return "D3DERR_TEXTURE_DESTROY_FAILED\n";
    case D3DERR_TEXTURE_LOCK_FAILED:
        return "D3DERR_TEXTURE_LOCK_FAILED\n";
    case D3DERR_TEXTURE_UNLOCK_FAILED:
        return "D3DERR_TEXTURE_UNLOCK_FAILED\n";
    case D3DERR_TEXTURE_LOAD_FAILED:
        return "D3DERR_TEXTURE_LOAD_FAILED\n";
    case D3DERR_MATRIX_CREATE_FAILED:
        return "D3DERR_MATRIX_CREATE_FAILED\n";
    case D3DERR_MATRIX_DESTROY_FAILED:
        return "D3DERR_MATRIX_DESTROY_FAILED\n";
    case D3DERR_MATRIX_SETDATA_FAILED:
        return "D3DERR_MATRIX_SETDATA_FAILED\n";
    case D3DERR_SETVIEWPORTDATA_FAILED:
        return "D3DERR_SETVIEWPORTDATA_FAILED\n";
    case D3DERR_MATERIAL_CREATE_FAILED:
        return "D3DERR_MATERIAL_CREATE_FAILED\n";
    case D3DERR_MATERIAL_DESTROY_FAILED:
        return "D3DERR_MATERIAL_DESTROY_FAILED\n";
    case D3DERR_MATERIAL_SETDATA_FAILED:
        return "D3DERR_MATERIAL_SETDATA_FAILED\n";
    case D3DERR_LIGHT_SET_FAILED:
        return "D3DERR_LIGHT_SET_FAILED\n";
    case D3DERR_INVALIDRAMPTEXTURE:
        return "D3DERR_INVALIDRAMPTEXTURE\n";
    case D3DERR_INVALIDVERTEXTYPE:
        return "D3DERR_INVALIDVERTEXTYPE\n";
    case D3DERR_INVALIDPRIMITIVETYPE:
        return "D3DERR_INVALIDPRIMITIVETYPE\n";
    case D3DERR_SCENE_NOT_IN_SCENE:
        return "Attempts to call rendering methods when a scene is not in progress!\nCall "
               "BeginScene() first.\n";
    case D3DERR_SCENE_IN_SCENE:
        return "Previous scene has not been completed\nCall EndScene() first!\n";
    case D3DERR_ZBUFF_NEEDS_VIDEOMEMORY:
        return "D3DERR_ZBUFF_NEEDS_VIDEOMEMORY\n";
    case D3DERR_WRONGTEXTUREFORMAT:
        return "D3DERR_WRONGTEXTUREFORMAT\n";
    case D3DERR_VIEWPORTHASNODEVICE:
        return "D3DERR_VIEWPORTHASNODEVICE\n";
    case D3DERR_ZBUFFER_NOTPRESENT:
        return "D3DERR_ZBUFFER_NOTPRESENT\n";
    case D3DERR_CONFLICTINGTEXTUREFILTER:
        return "D3DERR_CONFLICTINGTEXTUREFILTER\n";
    case D3DERR_CONFLICTINGTEXTUREPALETTE:
        return "D3DERR_CONFLICTINGTEXTUREPALETTE\n";
    case D3DERR_TOOMANYOPERATIONS:
        return "D3DERR_TOOMANYOPERATIONS\n";
    case D3DERR_UNSUPPORTEDALPHAARG:
        return "D3DERR_UNSUPPORTEDALPHAARG\n";
    case D3DERR_UNSUPPORTEDALPHAOPERATION:
        return "D3DERR_UNSUPPORTEDALPHAOPERATION\n";
    case D3DERR_UNSUPPORTEDCOLORARG:
        return "D3DERR_UNSUPPORTEDCOLORARG\n";
    case D3DERR_UNSUPPORTEDCOLOROPERATION:
        return "D3DERR_UNSUPPORTEDCOLOROPERATION\n";
    case D3DERR_UNSUPPORTEDFACTORVALUE:
        return "D3DERR_UNSUPPORTEDFACTORVALUE\n";
    case D3DERR_UNSUPPORTEDTEXTUREFILTER:
        return "D3DERR_UNSUPPORTEDTEXTUREFILTER\n";
    case D3DERR_SCENE_BEGIN_FAILED:
        return "D3DERR_SCENE_BEGIN_FAILED\n";
    case D3DERR_SCENE_END_FAILED:
        return "D3DERR_SCENE_END_FAILED\n";

    // DirectInput error codes
    case DIERR_OTHERAPPHASPRIO:
        return "Another application has a higher priority level, preventing this call from "
               "succeeding\n";
    case DIERR_NOTINITIALIZED:
        return "This object has not been initialized\n";
    case DIERR_INPUTLOST:
        return "Access to the input device has been lost. It must be reacquired\n";
    case DIERR_NOTACQUIRED:
        return "The operation cannot be performed unless the device is acquired\n";

    default:
        return "Unrecognized error value.\n";
    }

    return "Unrecognized error value.\n";
}
