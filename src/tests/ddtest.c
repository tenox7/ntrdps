/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ddraw.h>

static DWORD pack(DWORD mask, DWORD v)
{
    int shift = 0, bits = 0;
    DWORD m = mask;

    if (m == 0) {
        return 0;
    }
    while ((m & 1) == 0) {
        m >>= 1;
        shift++;
    }
    while (m & 1) {
        m >>= 1;
        bits++;
    }
    if (bits < 8) {
        v >>= 8 - bits;
    } else if (bits > 8) {
        v <<= bits - 8;
    }
    return (v << shift) & mask;
}

static int fail(const char *what, HRESULT hr)
{
    printf("ddtest: %s failed, HRESULT 0x%08lx\n", what, (unsigned long) hr);
    return 1;
}

static int lock_fill(LPDIRECTDRAWSURFACE s, RECT *r, DWORD pixel, int bpp)
{
    DDSURFACEDESC sd;
    HRESULT hr;
    BYTE *row, *p;
    LONG x, y;

    memset(&sd, 0, sizeof(sd));
    sd.dwSize = sizeof(sd);
    hr = s->lpVtbl->Lock(s, r, &sd, DDLOCK_WAIT, NULL);
    if (hr != DD_OK) {
        return fail("Lock", hr);
    }
    row = (BYTE *) sd.lpSurface;
    for (y = r->top; y < r->bottom; y++) {
        p = row;
        for (x = r->left; x < r->right; x++) {
            switch (bpp) {
            case 8:
                *p++ = (BYTE) pixel;
                break;
            case 16:
                *(WORD *) p = (WORD) pixel;
                p += 2;
                break;
            case 24:
                p[0] = (BYTE) pixel;
                p[1] = (BYTE) (pixel >> 8);
                p[2] = (BYTE) (pixel >> 16);
                p += 3;
                break;
            default:
                *(DWORD *) p = pixel;
                p += 4;
                break;
            }
        }
        row += sd.lPitch;
    }
    hr = s->lpVtbl->Unlock(s, sd.lpSurface);
    if (hr != DD_OK) {
        return fail("Unlock", hr);
    }
    return 0;
}

int main(int argc, char **argv)
{
    LPDIRECTDRAW dd = NULL;
    LPDIRECTDRAWSURFACE prim = NULL;
    DDSURFACEDESC sd;
    DDCAPS hw, hel;
    DDBLTFX fx;
    HRESULT hr;
    RECT r;
    DWORD rgb = 0xFF0000UL, pixel;
    int lock = 0, bpp, rc = 1, a = 1;

    if (argc > a && strcmp(argv[a], "lock") == 0) {
        lock = 1;
        a++;
    } else if (argc > a && strcmp(argv[a], "fill") == 0) {
        a++;
    }
    r.left = 100;
    r.top = 100;
    r.right = 400;
    r.bottom = 300;
    if (argc >= a + 4) {
        r.left = atol(argv[a]);
        r.top = atol(argv[a + 1]);
        r.right = r.left + atol(argv[a + 2]);
        r.bottom = r.top + atol(argv[a + 3]);
        a += 4;
    }
    if (argc > a) {
        rgb = strtoul(argv[a], NULL, 16);
    }

    hr = DirectDrawCreate(NULL, &dd, NULL);
    if (hr != DD_OK) {
        return fail("DirectDrawCreate", hr);
    }
    memset(&hw, 0, sizeof(hw));
    memset(&hel, 0, sizeof(hel));
    hw.dwSize = sizeof(hw);
    hel.dwSize = sizeof(hel);
    hr = dd->lpVtbl->GetCaps(dd, &hw, &hel);
    if (hr == DD_OK) {
        printf("ddtest: caps of the driver 0x%08lx (blt %s, color fill %s),"
               " of the emulation 0x%08lx\n", (unsigned long) hw.dwCaps,
               (hw.dwCaps & DDCAPS_BLT) ? "yes" : "no",
               (hw.dwCaps & DDCAPS_BLTCOLORFILL) ? "yes" : "no",
               (unsigned long) hel.dwCaps);
    } else {
        printf("ddtest: GetCaps failed, HRESULT 0x%08lx\n",
               (unsigned long) hr);
    }
    hr = dd->lpVtbl->SetCooperativeLevel(dd, NULL, DDSCL_NORMAL);
    if (hr != DD_OK) {
        fail("SetCooperativeLevel(DDSCL_NORMAL)", hr);
        goto done;
    }
    memset(&sd, 0, sizeof(sd));
    sd.dwSize = sizeof(sd);
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = dd->lpVtbl->CreateSurface(dd, &sd, &prim, NULL);
    if (hr != DD_OK) {
        fail("CreateSurface(primary)", hr);
        goto done;
    }
    memset(&sd, 0, sizeof(sd));
    sd.dwSize = sizeof(sd);
    hr = prim->lpVtbl->GetSurfaceDesc(prim, &sd);
    if (hr != DD_OK) {
        fail("GetSurfaceDesc", hr);
        goto done;
    }
    bpp = (int) sd.ddpfPixelFormat.dwRGBBitCount;
    if (sd.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) {
        pixel = rgb & 0xFF;
    } else {
        pixel = pack(sd.ddpfPixelFormat.dwRBitMask, (rgb >> 16) & 0xFF) |
                pack(sd.ddpfPixelFormat.dwGBitMask, (rgb >> 8) & 0xFF) |
                pack(sd.ddpfPixelFormat.dwBBitMask, rgb & 0xFF);
    }
    printf("ddtest: primary surface %lu x %lu, %d bpp, masks %06lx %06lx "
           "%06lx, pitch %ld\n", (unsigned long) sd.dwWidth,
           (unsigned long) sd.dwHeight, bpp,
           (unsigned long) sd.ddpfPixelFormat.dwRBitMask,
           (unsigned long) sd.ddpfPixelFormat.dwGBitMask,
           (unsigned long) sd.ddpfPixelFormat.dwBBitMask, (long) sd.lPitch);
    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
        printf("ddtest: %d bpp is not supported here\n", bpp);
        goto done;
    }
    if (lock) {
        if (lock_fill(prim, &r, pixel, bpp) != 0) {
            goto done;
        }
    } else {
        memset(&fx, 0, sizeof(fx));
        fx.dwSize = sizeof(fx);
        fx.dwFillColor = pixel;
        hr = prim->lpVtbl->Blt(prim, &r, NULL, NULL,
                               DDBLT_COLORFILL | DDBLT_WAIT, &fx);
        if (hr != DD_OK) {
            fail("Blt(DDBLT_COLORFILL)", hr);
            goto done;
        }
    }
    printf("ddtest: %s (%ld,%ld)-(%ld,%ld) with pixel 0x%lx (color %06lx):"
           " done\n", lock ? "lock" : "fill", (long) r.left, (long) r.top,
           (long) r.right, (long) r.bottom, (unsigned long) pixel,
           (unsigned long) rgb);
    rc = 0;
done:
    if (prim != NULL) {
        prim->lpVtbl->Release(prim);
    }
    dd->lpVtbl->Release(dd);
    return rc;
}
