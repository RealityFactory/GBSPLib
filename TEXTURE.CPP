/****************************************************************************************/
/*  Texture.cpp                                                                         */
/*                                                                                      */
/*  Author: John Pollard                                                                */
/*  Description: This code keeps a list of shared textures.                             */
/*                                                                                      */
/*  Edit History:                                                                       */
/*   11/11/2003 Wendell Buckner                                                         */
/*    Bumpmapping for the World                                                         */
/*   10/15/2003 Wendell Buckner                                                         */
/*    Bumpmapping for the World                                                         */
/*                                                                                      */
/*  The contents of this file are subject to the Genesis3D Public License               */
/*  Version 1.01 (the "License"); you may not use this file except in                   */
/*  compliance with the License. You may obtain a copy of the License at                */
/*  http://www.genesis3d.com                                                            */
/*                                                                                      */
/*  Software distributed under the License is distributed on an "AS IS"                 */
/*  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See                */
/*  the License for the specific language governing rights and limitations              */
/*  under the License.                                                                  */
/*                                                                                      */
/*  The Original Code is Genesis3D, released March 25, 1999.                            */
/*  Genesis3D Version 1.1 released November 15, 1999                                    */
/*  Copyright (C) 1999 WildTangent, Inc. All Rights Reserved                            */
/*                                                                                      */
/****************************************************************************************/
#include <Windows.h>

#include <Stdio.h>
#include <Assert.h>

#include "Map.h"
#include "BSP.h"
#include "Texture.h"
#include "Utils.h"

#include	"vfile.h"
#include	"bitmap.h"
#include	"ram.h"

//#define USEPERPIXEL

int32		NumTextures;
GFX_Texture	Textures[MAX_MAP_TEXTURES];
int32		NumTexInfo;
GFX_TexInfo	TexInfo[MAX_MAP_TEXINFO];

typedef	struct	NamedBitmap
{
	char *		Name;
	geBitmap *	Bitmap;
}	NamedBitmap;

static	int				NumBitmaps;
static	NamedBitmap *	Bitmaps;

//========================================================================================
//	FindTextureIndex
//========================================================================================
int32 FindTextureIndex(char *Name, uint32 Flags)
{
	int32		i;

	for (i=0; i< NumTextures; i++)
	{
		if (!stricmp(Name, Textures[i].Name) && Textures[i].Flags == Flags)
			return i;
	}

	if (NumTextures >= MAX_MAP_TEXTURES)
	{
		GHook.Error("FindTextureIndex:  Too many Textures in map.");
		return -1;
	}

/* 10/15/2003 Wendell Buckner
    Bumpmapping for the World */
	do
	{
	 char *GreyBumpmapName = NULL;

/* 11/11/2003 Wendell Buckner
    Bumpmapping for the World */
		char *SpecularBumpmapName = NULL;

		NamedBitmap *	Bitmap = NULL;

//Check for base bumpmap name
		if (!geBitmap_IsBumpmapName(Name)) break;

//Add grey bumpmap name
		GreyBumpmapName = geBitmap_GetGreyBumpmapName(Name);
		if(!GreyBumpmapName )break;

		strcpy(Textures[NumTextures].Name, GreyBumpmapName);
		Textures[NumTextures].Flags = Flags;

		NumTextures++;

/* 11/11/2003 Wendell Buckner
    Bumpmapping for the World */

//Add specular bumpmap name
		SpecularBumpmapName = geBitmap_GetSpecularBumpmapName(Name);
		if(!SpecularBumpmapName )break;

		strcpy(Textures[NumTextures].Name, SpecularBumpmapName);
		Textures[NumTextures].Flags = Flags;

		NumTextures++;
	}
	while(GE_FALSE);

	strcpy(Textures[NumTextures].Name, Name);
	Textures[NumTextures].Flags = Flags;

	NumTextures++;

	//Hook.Printf("Adding Texture: %s\n", Name);

	return(NumTextures-1);
}

int32 NumSurfaceLights;

//========================================================================================
//	FindTexInfo
//========================================================================================
int32 FindTexInfo(GFX_TexInfo *Tex2)
{
	int32			i;
	GFX_TexInfo	*Tex1;

	if ((Tex2->Flags & TEXINFO_FLAT))
		Tex2->Flags |= TEXINFO_GOURAUD;

	if (Tex2->Flags & TEXINFO_SKY)				// Force sky to fullbright
		Tex2->Flags |= TEXINFO_FULLBRIGHT;

	if ((Tex2->Flags & TEXINFO_SKY) || (Tex2->Flags & TEXINFO_FULLBRIGHT))	
		Tex2->Flags |= TEXINFO_NO_LIGHTMAP;

	if ((Tex2->Flags & TEXINFO_GOURAUD))
		Tex2->Flags |= TEXINFO_NO_LIGHTMAP;

	for (i=0; i< NumTexInfo; i++)
	{
		Tex1 = &TexInfo[i];

		if (Tex1->Vecs[0].X == Tex2->Vecs[0].X)
		if (Tex1->Vecs[0].Y == Tex2->Vecs[0].Y)
		if (Tex1->Vecs[0].Z == Tex2->Vecs[0].Z)
		if (Tex1->Vecs[1].X == Tex2->Vecs[1].X)
		if (Tex1->Vecs[1].Y == Tex2->Vecs[1].Y)
		if (Tex1->Vecs[1].Z == Tex2->Vecs[1].Z)
		if (Tex1->Shift[0] == Tex2->Shift[0])
		if (Tex1->Shift[1] == Tex2->Shift[1])
		if (Tex1->DrawScale[0] == Tex2->DrawScale[0])
		if (Tex1->DrawScale[1] == Tex2->DrawScale[1])
		if (Tex1->Flags == Tex2->Flags)
		if (Tex1->FaceLight == Tex2->FaceLight)
		if (Tex1->ReflectiveScale == Tex2->ReflectiveScale)
		if (Tex1->Alpha == Tex2->Alpha)
		if (Tex1->MipMapBias == Tex2->MipMapBias)
		if (Tex1->Texture == Tex2->Texture)
			return i;
	}

	if (NumTexInfo >= MAX_MAP_TEXINFO)
	{
		GHook.Error("FindTexInfo:  Too much texture information...\n");
		return -1;
	}

	TexInfo[i] = *Tex2;
	NumTexInfo++;

	return i;
}

geBoolean InitTextureLib(char *FileName)
{
	char				Buff[_MAX_PATH];
	geVFile *			VFS;
	geVFile_Finder *	Finder;
	int					i;

	Finder = NULL;

	strcpy(Buff, FileName);
	StripExtension(Buff);
	DefaultExtension(Buff, ".txl");

	VFS = geVFile_OpenNewSystem(NULL,
								GE_VFILE_TYPE_VIRTUAL,
								Buff,
								NULL,
								GE_VFILE_OPEN_DIRECTORY | GE_VFILE_OPEN_READONLY);
	if	(!VFS)
		goto fail;

	Finder = geVFile_CreateFinder(VFS, "*.*");
	if	(!Finder)
		goto fail;

	i = 0;
	while	(geVFile_FinderGetNextFile(Finder) == GE_TRUE)
	{
		i++;
	}

	geVFile_DestroyFinder(Finder);

	Finder = geVFile_CreateFinder(VFS, "*.*");

	if	(!Finder)
		goto fail;

	NumBitmaps = i;
	Bitmaps = (NamedBitmap *)geRam_Allocate(sizeof(*Bitmaps) * NumBitmaps);

	if	(!Bitmaps)
		goto fail;

	memset(Bitmaps, 0, sizeof(*Bitmaps) * NumBitmaps);

	i = 0;
	while	(geVFile_FinderGetNextFile(Finder) == GE_TRUE)
	{
		geVFile_Properties	Properties;
		geVFile *			File;

		geVFile_FinderGetProperties(Finder, &Properties);
		Bitmaps[i].Name = strdup(Properties.Name);
		if	(!Bitmaps[i].Name)
		{
//		GHook.Error("InitTextures:  5 Unable to load texture library '%s'.\n", Buff);
			goto fail;
		}
		File = geVFile_Open(VFS, Properties.Name, GE_VFILE_OPEN_READONLY);
		if	(!File)
		{
			GHook.Error("InitTextures:  Unable to load texture file '%s'.\n", Properties.Name);
			goto fail;
		}
		Bitmaps[i].Bitmap = geBitmap_CreateFromFile(File);
		geVFile_Close(File);
		if	(!Bitmaps[i].Bitmap)
		{
			GHook.Error("InitTextures:  Unable to load texture '%s'.\n", Properties.Name);
			goto fail;
		}
//		GHook.Printf("InitTextures:  Loaded texture '%s'.\n", Properties.Name);
		i++;
	}

	geVFile_Close(VFS);
	return GE_TRUE;

fail:
	if	(VFS)
		geVFile_Close(VFS);

	if	(Finder)
		geVFile_DestroyFinder(Finder);
	
	ShutdownTextureLib();

	GHook.Error("InitTextures:  Unable to load texture library '%s'.\n", Buff);

	return GE_FALSE;
}

//========================================================================================
//========================================================================================
void ShutdownTextureLib(void)
{
	if	(Bitmaps)
	{
		int	i;

		for	(i = 0; i < NumBitmaps; i++)
		{
			if	(Bitmaps[i].Name)
				free(Bitmaps[i].Name);
			if	(Bitmaps[i].Bitmap)
				geBitmap_Destroy(&Bitmaps[i].Bitmap);
		}
		geRam_Free(Bitmaps);
		Bitmaps = NULL;
	}

	NumBitmaps = 0;
}

static	NamedBitmap *	FindBitmapByName(const char *Name)
{
	if	(Bitmaps)
	{
		int	i;

		for	(i = 0; i < NumBitmaps; i++)
		{
			if	(!stricmp(Bitmaps[i].Name, Name))
			{
				return &Bitmaps[i];
			}
		}
	}

	return NULL;
}

// added transparent textures
geBoolean HasTextureAlpha(char *Name)
{
	geBitmap *		Image;
	NamedBitmap *	Bitmap;

	Bitmap = FindBitmapByName(Name);

	if	(Bitmap == NULL)
	{
		GHook.Printf("Could not find texture alpha'%s' in texture library.\n", Name);
		if	(!Bitmaps)
		{
			GHook.Error("Could not find any textures in texture library.\n");
			return GE_FALSE;
		}

		assert(NumBitmaps > 0);

		Name  = Bitmaps[0].Name;
		Image = Bitmaps[0].Bitmap;
	}
	else
	{
		Image = Bitmap->Bitmap;
	}

	return geBitmap_HasAlpha(Image);
}
// end transparent textures

//========================================================================================
//========================================================================================
geBoolean GetTexture(char *Name, uint8 *Data, int32 *Size, int32 *Width, int32 *Height, geVFile *f)
{
	int32			i;
	geBitmap *		Image;
	NamedBitmap *	Bitmap;

	Bitmap = FindBitmapByName(Name);
	
	if	(Bitmap == NULL)
	{
		GHook.Printf("Could not find texture '%s' in texture library.\n", Name);
		if	(!Bitmaps)
		{
			GHook.Error("Could not find any textures in texture library.\n");
			return GE_FALSE;
		}

		assert(NumBitmaps > 0);

/* 10/15/2003 Wendell Buckner
    Bumpmapping for the World */
		if( geBitmap_IsGreyBumpmapName(Name) || geBitmap_IsSpecularBumpmapName(Name) ) strcpy(Name,Bitmaps[0].Name);

		Name  = Bitmaps[0].Name;
		Image = Bitmaps[0].Bitmap;
	}
	else
	{
		Image = Bitmap->Bitmap;
	}

	//GHook.Printf("%s\n", Name);

	*Width = geBitmap_Width(Image);
	*Height = geBitmap_Height(Image);

/* 11/24/2002 Wendell Buckner
    Allow much higher dimensions than 256 x 256
	if	(*Width > 256 || *Height > 256)
	{
		GHook.Error("Texture '%s' has a dimension bigger than 256.\n", Name);
		return GE_FALSE;
	}	*/
	if	( *Width > 16384 || *Height > 16384 )
	{
		GHook.Error("Texture '%s' has a dimension bigger than 16384.\n", Name);
		return GE_FALSE;
	}

	*Size = 0;
	geBitmap_UpdateMips(Image, 0, 1);
	geBitmap_UpdateMips(Image, 1, 2);
	geBitmap_UpdateMips(Image, 2, 3);
// added transparent textures - only 1 mip sent
	for	(i = 0; i < 1; i++)
	{
		int			MipWidth;
		int			MipHeight;
		geBitmap *	LockedImage;
		void *		Bits;
		uint8		depth;
		geBoolean	lock;

		depth = GE_PIXELFORMAT_24BIT_RGB;
		if(geBitmap_HasAlpha(Image))
			depth = GE_PIXELFORMAT_32BIT_ARGB;
		geVFile_Write(f, &depth, 1);
		*Size += 1;

		if(depth == GE_PIXELFORMAT_24BIT_RGB)
			lock = geBitmap_LockForRead(Image, &LockedImage, i, i, GE_PIXELFORMAT_24BIT_RGB, GE_TRUE, 0xffff00ff);
		else
			lock = geBitmap_LockForRead(Image, &LockedImage, i, i, GE_PIXELFORMAT_32BIT_ARGB, GE_TRUE, 0xffff00ff);

//Start Dec2001DCS
// added transparent textures
//#ifdef USEPERPIXEL
//	  if(geBitmap_LockForRead(Image, &LockedImage, i, i, GE_PIXELFORMAT_32BIT_ARGB, GE_TRUE, 0xffff00ff) == GE_FALSE)
//#else
//	  if(geBitmap_LockForRead(Image, &LockedImage, i, i, GE_PIXELFORMAT_24BIT_RGB, GE_TRUE, 0xffff00ff) == GE_FALSE)
//#endif
//End Dec2001DCS
		if(lock == GE_FALSE)
		{
			GHook.Error("Could not get mip level %d for texture '%s'.\n", i, Name);
			return GE_FALSE;
		}
		
		Bits = geBitmap_GetBits(LockedImage);
		if	(!Bits)
		{
			GHook.Error("Could not get mip bits for texture '%s'.\n", Name);
			return GE_FALSE;
		}

		MipWidth = *Width / (1 << i);
		MipHeight = *Height / (1 << i);

		if(depth == GE_PIXELFORMAT_24BIT_RGB)
			lock = geVFile_Write(f, Bits, MipWidth * MipHeight * 3);
		else
			lock = geVFile_Write(f, Bits, MipWidth * MipHeight * 4);

//Start Dec2001DCS - Added * 4 since textures are now 32 bit
// added transparent textures
//#ifdef USEPERPIXEL
//		if (geVFile_Write(f, Bits, MipWidth * MipHeight * 4) != GE_TRUE)
//#else
//		if (geVFile_Write(f, Bits, MipWidth * MipHeight * 3) != GE_TRUE)
//#endif
//End Dec2001DCS
		if (lock != GE_TRUE)
		{
			GHook.Error("Could not write texture data.\n");
			return GE_FALSE;
		}

		geBitmap_UnLock(LockedImage);

//Start Dec2001DCS - Added * 4 since textures are now 32 bit
// added transparent textures
	if(depth == GE_PIXELFORMAT_24BIT_RGB)
		*Size += MipWidth * MipHeight * 3;
	else
		*Size += MipWidth * MipHeight * 4;

//End Dec2001DCS
	}

	return GE_TRUE;
}

geBoolean GetTexturePalette(const char *Name, DRV_Palette Palette)
{
	geBitmap *			Image;
	NamedBitmap *		Bitmap;
	geBitmap_Palette *	ImagePalette;
	int					i;

	assert(Name != NULL);
	assert(Palette != NULL);

	Bitmap = FindBitmapByName(Name);
	
	if	(Bitmap == NULL)
	{
		GHook.Printf("Could not find texture '%s' in texture library.\n", Name);
		if	(!Bitmaps)
		{
			GHook.Error("Could not find any textures in texture library.\n");
			return GE_FALSE;
		}

		Name  = Bitmaps[0].Name;
		Image = Bitmaps[0].Bitmap;
	}
	else
	{
		Image = Bitmap->Bitmap;
	}

	ImagePalette = geBitmap_GetPalette(Image);
	if	(!ImagePalette)
	{
//Start Dec2001DCS
//		GHook.Error("Could not find get pallette for texture '%s'.\n", Name);
//		return GE_FALSE;
// No palette found is not an error anymore - 24 bit color bitmaps do not have a palette
      return GE_TRUE;
//End Dec2001DCS
	}

	for	(i = 0; i < 255; i++)
	{
		int	A;
		int	R;
		int	G;
		int	B;
		geBitmap_Palette_GetEntryColor(ImagePalette, i, &R, &G, &B, &A);
		Palette[i].r = R;
		Palette[i].g = G;
		Palette[i].b = B;
	}

	return GE_TRUE;
}

