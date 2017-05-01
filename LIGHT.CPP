/****************************************************************************************/
/*  Light.cpp                                                                           */
/*                                                                                      */
/*  Author: John Pollard                                                                */
/*  Description: Lights a BSP                                                           */
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
/*Genesis3D Version 1.1 released November 15, 1999                            */
/*  Copyright (C) 1999 WildTangent, Inc. All Rights Reserved           */
/*                                                                                      */
/****************************************************************************************/
#include <Windows.h>
#include <Stdio.h>
#include <Assert.h>

#include "Math.h"
#include "MathLib.h"
#include "GBSPFile.h"
#include "Light.h"
#include "Map.h"
#include "Texture.h"
#include "Utils.h"
#include "BSP.h"

#include "Vec3d.h"
#include "XForm3d.h"

#include "Ram.h"

#define RWM_GOURAUD 1
#define DEBUG_RWM_GOURAUD 1

float		LightScale		= 1.00f;
float		EntityScale		= 1.00f;
float		MaxLight		= 230.0f;
int32		NumSamples		= 5;


geBoolean	LVerbose				= GE_TRUE;
geBoolean	DoRadiosity				= GE_TRUE;
float		PatchSize				= 128.0f;
int32		NumBounce				= 8;
geBoolean	FastPatch				= GE_TRUE;
geBoolean	ExtraLightCorrection	= GE_TRUE;
float		ReflectiveScale			= 1.0f;

geVec3d		MinLight;

int32		NumLMaps;
int32		LightOffset;

int32		RGBMaps = 0;
int32		REGMaps = 0;

LInfo		*Lightmaps;
FInfo		*FaceInfo;

geVec3d		*VertNormals;

void		FinalizeRGBVerts(void);
void		FinalizeRGBVert( geVec3d* a_Color ) ;
geBoolean	LightFaces(int a_GouraudShading);
geBoolean	MakeVertNormals(void);
geBoolean	SaveLightmaps(geVFile *f);
void		FreeLightmaps(void);
geBoolean	ApplyLightsToFace(FInfo *FaceInfo, LInfo *LightInfo, float Scale);
geBoolean	GouraudShadeFace(int32 FaceNum);
void		GetFacePlane(int32 Face, GFX_Plane *Plane);
geBoolean	CalcFaceInfo(FInfo *FaceInfo, LInfo *LightInfo);
void		CalcFacePoints(FInfo *FaceInfo, LInfo *LightInfo, float UOfs, float VOfs);
float		PlaneDistanceFast(geVec3d *Point, GFX_Plane *Plane);
geBoolean	CreateDirectLights(void);
void		FreeDirectLights(void);

geBoolean	StartWriting(geVFile *f);
geBoolean	FinishWriting(geVFile *f);

// For RayIntersect
int32		GlobalPlane;
int32		GlobalNode;
int32		GlobalSide;
geVec3d		GlobalI;

#define MAX_DIRECT_CLUSTER_LIGHTS		25000
#define MAX_DIRECT_LIGHTS				5000

typedef enum
{
	DLight_Blank,
	DLight_Point,
	DLight_Spot,
	DLight_Surface,
	// LWM: start
	DLight_Sun,
	// LWM: end
	// Wismerhill
	DLight_SunLight
} Light_DirectLightType;

typedef struct Light_DirectLight
{
	Light_DirectLight		*Next;
	int32					LType;
	geVec3d					Origin;
	geVec3d					Normal;
	float					Angle;
	geVec3d					Color;
	float					Intensity;
	Light_DirectLightType	Type;
	// LWM: start
	int					SunFallOffType ;
	float					SunFallOffRadiusInTexels ;
	float					SunFallOff100PercentRadius ;
	float					SunFallOffAlpha ;
	bool					SunRGBOnly ;
	bool					SunRGBAlwaysVisible ;
	// LWM: end
} Light_DirectLight;

// LWM: start
#define SUN_MIN_FALLOFF_TYPE    0
#define SUN_FALLOFF_NONE        0
#define SUN_FALLOFF_HARD_RADIUS 1
#define SUN_FALLOFF_GRADUALLY1  2
#define SUN_FALLOFF_GRADUALLY2  3
#define SUN_MAX_FALLOFF_TYPE    3

#define DEBUG_SUN 0
// LWM: end

Light_DirectLight	*DirectClusterLights[MAX_DIRECT_CLUSTER_LIGHTS];
Light_DirectLight	*DirectLights[MAX_DIRECT_LIGHTS];
int32				NumDirectLights = 0;

#if DEBUG_RWM_GOURAUD
static void DebugWriteVertexSetFileName( const char* a_FileName ) ;
static void DebugWriteVertex( 
	const geVec3d* a_Vert, 
	const geVec3d* a_Color, 
	const geVec3d* a_Normal, 
	int a_LightsCount, Light_DirectLight** a_LightsFound );
#endif

//====================================================================================
//	LightGBSPFile
//====================================================================================
geBoolean LightGBSPFile(char *FileName, LightParms *Parms)
{
	geVFile	*f;
	char	PalFile[MAX_PATH];
	char	RecFile[MAX_PATH];

#if DEBUG_RWM_GOURAUD
	// TODO: remove?
	GHook.Printf("FileName %s\n", FileName ) ;
	DebugWriteVertexSetFileName( FileName ) ;
#endif

	f = NULL;

	LVerbose = GE_TRUE;

	if (Parms->ExtraSamples)
		NumSamples = 5;
	else
		NumSamples = 1;

	EntityScale = Parms->LightScale;
	DoRadiosity = Parms->Radiosity;
	NumBounce = Parms->NumBounce;
	PatchSize = Parms->PatchSize;
	FastPatch = Parms->FastPatch;
	ReflectiveScale = Parms->ReflectiveScale;

	MinLight = Parms->MinLight;

	GHook.Printf(" --- Radiosity GBSP File --- \n");
	
	if (!LoadGBSPFile(FileName))
	{
		GHook.Error("LightGBSPFile:  Could not load GBSP file: %s.\n", FileName);
		return GE_FALSE;
	}

	// Allocate some RGBLight data now
	NumGFXRGBVerts = NumGFXVertIndexList;
	GFXRGBVerts = GE_RAM_ALLOCATE_ARRAY(geVec3d,NumGFXRGBVerts);
	memset(GFXRGBVerts, 0, sizeof(geVec3d)*NumGFXRGBVerts);

	if (!MakeVertNormals())
	{
		GHook.Error("LightGBSPFile:  MakeVertNormals failed...\n");
		goto ExitWithError;
	}

	// Make sure no existing light exist...
	if (GFXLightData)
		geRam_Free(GFXLightData);

	GFXLightData = NULL;
	NumGFXLightData = 0;

	// Get the palette file name
	strcpy(PalFile, FileName);
	StripExtension(PalFile);
	DefaultExtension(PalFile, ".PAL");

	// Get the receiver file name
	strcpy(RecFile, FileName);
	StripExtension(RecFile);
	DefaultExtension(RecFile, ".REC");

	if (!ConvertGFXEntDataToEntities())
		goto ExitWithError;

	f = geVFile_OpenNewSystem(NULL, GE_VFILE_TYPE_DOS, FileName, NULL, GE_VFILE_OPEN_CREATE);
	
	if (!f)
	{
		GHook.Error("LightGBSPFile:  Could not open GBSP file for writing: %s.\n", FileName);
		goto ExitWithError;
	}

	GHook.Printf("Num Faces            : %5i\n", NumGFXFaces);

	// Build the patches (before direct lights are created)
	if (DoRadiosity)
	{
		if (!BuildPatches())
			goto ExitWithError;
	}

	if (!CreateDirectLights())
	{
		GHook.Error("LightGBSPFile:  Could not create main lights.\n");
		goto ExitWithError;
	}
	
	// Light faces, and apply to patches
	if (!LightFaces(Parms->Verbose))		// Light all the faces lightmaps, and apply to patches
		goto ExitWithError;

	FreeDirectLights();

	if (DoRadiosity)
	{
		// Pre-calc how much light is distributed to each patch from every patch
		if (!CalcReceivers(RecFile))	
			goto ExitWithError;

		// Bounce patches around to their receivers
		if (!BouncePatches())	// Bounce them around
			goto ExitWithError;
	
		FreeReceivers();		// Don't need these anymore

		// Apply the patches back into the light maps
		if (!AbsorbPatches())	// Apply the patches to the lightmaps
			goto ExitWithError;
	
		FreePatches();			// Don't need these anymore...
	}

	FinalizeRGBVerts();

	if (!StartWriting(f))	// Open bsp file and save all current bsp data (except lightmaps)
		goto ExitWithError;

	if (!SaveLightmaps(f))	// Save them 
		goto ExitWithError;

	if (!FinishWriting(f))	// Write the END chunk to the file
		goto ExitWithError;

	geVFile_Close(f);				
	CleanupLight();

	GHook.Printf("Num Light Maps       : %5i\n", RGBMaps);

	return GE_TRUE;

	ExitWithError:
	{
		if (f)
			geVFile_Close(f);

		CleanupLight();

		return GE_FALSE;
	}
}

//====================================================================================
//	CleanuoLight
//====================================================================================
void CleanupLight(void)
{
	// Don't need these anymore
	FreeDirectLights();
	FreePatches();
	FreeLightmaps();
	FreeReceivers();	

	if (VertNormals)
	{
		geRam_Free(VertNormals);
		VertNormals = NULL;
	}

	FreeGBSPFile();		// Free rest of GBSP GFX data
}


//====================================================================================
//	FinalizeRGBVerts
//====================================================================================
void FinalizeRGBVert( geVec3d* a_Color )
{
	// add ambient light (min light)
	geVec3d_Add(a_Color, &MinLight, a_Color);
	// force (scale) into range 0..255
	ColorClamp(a_Color, MaxLight, a_Color);
}

//====================================================================================
//	FinalizeRGBVerts
//====================================================================================
void FinalizeRGBVerts(void)
{
	int32		i;

	for (i=0; i< NumGFXRGBVerts; i++)
	{
		FinalizeRGBVert( &GFXRGBVerts[i] ) ;
	}
}


//====================================================================================
//	MakeVertNormals
//====================================================================================
geBoolean MakeVertNormals(void)
{
	GFX_Face	*pGFXFace;
	int32		i;

	VertNormals = GE_RAM_ALLOCATE_ARRAY(geVec3d, NumGFXVerts);

	if (!VertNormals)
	{
		GHook.Error("MakeVertNormals:  Out of memory for normals.\n");
		return GE_FALSE;
	}

	memset(VertNormals, 0, sizeof(geVec3d)*NumGFXVerts);

	pGFXFace = GFXFaces;

	for (i=0; i< NumGFXFaces; i++, pGFXFace++)
	{
		int32		v;
		geVec3d		Normal;

		Normal = GFXPlanes[pGFXFace->PlaneNum].Normal;

		if (pGFXFace->PlaneSide)
			geVec3d_Inverse(&Normal);

		for (v=0; v< pGFXFace->NumVerts; v++)
		{
			int32		vn, Index;

			vn = pGFXFace->FirstVert + v;

			Index = GFXVertIndexList[vn];

			geVec3d_Add(&VertNormals[Index], &Normal, &VertNormals[Index]);
		}
	}

	for (i=0; i< NumGFXVerts; i++)
	{
		geVec3d_Normalize(&VertNormals[i]);
	}

	return GE_TRUE;
}

//====================================================================================
//	TransferLightToPatches
//====================================================================================
void TransferLightToPatches(int32 Face)
{
	GFX_Face	*pGFXFace;
	RAD_Patch	*Patch;

	pGFXFace = &GFXFaces[Face];

	for (Patch = FacePatches[Face] ;Patch ; Patch=Patch->Next)
	{
		geVec3d		*pRGB, *pVert;
		int32		i;

		pRGB = &GFXRGBVerts[pGFXFace->FirstVert];

		Patch->NumSamples = 0;
		//geVec3d_Clear(&Patch->RadStart);

		for (i=0; i< pGFXFace->NumVerts; i++)
		{
			int32		k;

			pVert = &GFXVerts[GFXVertIndexList[i+pGFXFace->FirstVert]];

			for (k=0 ; k<3 ; k++)
			{
				if (VectorToSUB(Patch->Mins, k) > VectorToSUB(pVert[0], k) + 16)
					break;
		
				if (VectorToSUB(Patch->Maxs, k) < VectorToSUB(pVert[0], k) - 16)
					break;
			}

			if (k == 3)
			{
				// Add the Color to the patch 
				Patch->NumSamples++;
				geVec3d_Add(&Patch->RadStart, pRGB, &Patch->RadStart);
			}

			pRGB++;
		}
		
		if (Patch->NumSamples)
			geVec3d_Scale(&Patch->RadStart, 1.0f/(float)Patch->NumSamples, &Patch->RadStart);
	}

}

//====================================================================================
//	ApplyLightmapToPatches
//====================================================================================
void ApplyLightmapToPatches(int32 Face)
{
	int32		i, k;
	geVec3d		*pVert, *pRGB;//, RGB;
	RAD_Patch	*Patch;
	
	pRGB = Lightmaps[Face].RGBLData[0];	// Only contribute light type 0 to patches

	if (!pRGB)
		return;

	// Check each patch and see if the points lands in it's BBox
	for (Patch = FacePatches[Face] ;Patch ; Patch=Patch->Next)
	{
		pVert = FaceInfo[Face].Points;
		pRGB = Lightmaps[Face].RGBLData[0];	// Only contribute light type 0 to patches

		Patch->NumSamples = 0;
		//geVec3d_Clear(&Patch->RadStart);

		for (i=0; i< FaceInfo[Face].NumPoints; i++)
		{
			//geVec3d_Scale(pRGB, 1.2f, &RGB);

			//if (pRGB->X + pRGB->Y + pRGB->Z < 3)
			//	continue;		// Not enought light to worry about

			for (k=0 ; k<3 ; k++)
			{
				if (VectorToSUB(Patch->Mins, k) > VectorToSUB(pVert[0], k) + 16)
					break;
		
				if (VectorToSUB(Patch->Maxs, k) < VectorToSUB(pVert[0], k) - 16)
					break;
			}

			if (k == 3)
			{
				// Add the Color to the patch 
				Patch->NumSamples++;
				geVec3d_Add(&Patch->RadStart, pRGB, &Patch->RadStart);
			}

			pRGB++;
			pVert++;
		}
		
		if (Patch->NumSamples)
			geVec3d_Scale(&Patch->RadStart, 1.0f/(float)Patch->NumSamples, &Patch->RadStart);

		//if (Patch->RadStart.X < 16 && Patch->RadStart.Y < 16 && Patch->RadStart.Z < 16)
		//	geVec3d_Clear(&Patch->RadStart);

	}
}

float UOfs[5] = { 0.0f,-0.5f, 0.5f, 0.5f,-0.5f};
float VOfs[5] = { 0.0f,-0.5f,-0.5f, 0.5f, 0.5f};

//====================================================================================
//	LightFaces
//====================================================================================
geBoolean LightFaces(int a_GouraudShading)
{
	int32		i, s;
	geBoolean	Hit;
	int32		Perc;

#if DEBUG_RWM_GOURAUD
	if( a_GouraudShading )
	{
		GHook.Printf("Using RWM Gouraud coloring %d RGB faces\n", NumGFXRGBVerts );
	}
	else
	{
		GHook.Printf("Skipping RWM Gouraud coloring %d RGB faces (Full Vis only)\n", NumGFXRGBVerts );
	}
#endif

	Lightmaps = GE_RAM_ALLOCATE_ARRAY(LInfo,NumGFXFaces);

	if (!Lightmaps)
	{
		GHook.Error("LightFaces:  Out of memory for Lightmaps.\n");
		return GE_FALSE;
	}

	FaceInfo = GE_RAM_ALLOCATE_ARRAY(FInfo,NumGFXFaces);

	if (!FaceInfo)
	{
		GHook.Error("LightFaces:  Out of memory for FaceInfo.\n");
		return GE_FALSE;
	}

	memset(Lightmaps, 0, sizeof(LInfo) * NumGFXFaces);
	memset(FaceInfo, 0, sizeof(FInfo) * NumGFXFaces);

	NumLMaps = 0;

	Perc = NumGFXFaces / 20;

	for (i=0; i< NumGFXFaces; i++)
	{
		Hit = GE_FALSE;

		if (CancelRequest)
		{
			GHook.Printf("Cancel requested...\n");
			return GE_FALSE;
		}

		if (Perc)
		{
			if (!(i%Perc) && (i/Perc) <= 20)
				GHook.Printf(".%i", (i/Perc));
		}
		
		GetFacePlane(i, &FaceInfo[i].Plane);
		FaceInfo[i].Face = i;

#if RWM_GOURAUD
		if( a_GouraudShading )
		{
			// always RGB (also for LM faces)
			if (!GouraudShadeFace(i))
			{
				GHook.Error("LightFaces:  GouraudShadeFace failed...\n");
				return GE_FALSE;
			}
		}
		
		if (DoRadiosity)
			TransferLightToPatches(i);
#else
		// only RGB coloring for Gouraud shaded faces
		if (GFXTexInfo[GFXFaces[i].TexInfo].Flags & TEXINFO_GOURAUD)
		{
			if (!GouraudShadeFace(i))
			{
				GHook.Error("LightFaces:  GouraudShadeFace failed...\n");
				return GE_FALSE;
			}
			
			if (DoRadiosity)
				TransferLightToPatches(i);
			
			continue; // NOTE: (!)
		}
#endif
		
		/*
		if (GFXTexInfo[GFXFaces[i].TexInfo].Flags & TEXINFO_FLAT)
		{
			FlatShadeFace(i);
			continue;
		}
		*/

		if (GFXTexInfo[GFXFaces[i].TexInfo].Flags & TEXINFO_NO_LIGHTMAP)
			continue;		// Faces with no lightmap don't need to light them 


		if (!CalcFaceInfo(&FaceInfo[i], &Lightmaps[i]))
		{
			return GE_FALSE;
		}
	
		int32 Size = (Lightmaps[i].LSize[0]+1)*(Lightmaps[i].LSize[1]+1);
		FaceInfo[i].Points = GE_RAM_ALLOCATE_ARRAY(geVec3d, Size);

		if (!FaceInfo[i].Points)
		{
			GHook.Error("LightFaces:  Out of memory for face points.\n");
			return GE_FALSE;
		}
		
		for (s=0; s< NumSamples; s++)
		{
			//Hook.Printf("Sample  : %3i of %3i\n", s+1, NumSamples);
			CalcFacePoints(&FaceInfo[i], &Lightmaps[i], UOfs[s], VOfs[s]);

			if (!ApplyLightsToFace(&FaceInfo[i], &Lightmaps[i], 1 / (float)NumSamples))
				return GE_FALSE;
		}
		
		if (DoRadiosity)
		{
			// Update patches for this face
			ApplyLightmapToPatches(i);
		}
	}
	
	GHook.Printf("\n");

	return GE_TRUE;
}

// LWM: start
float SunCalcLightValue( Light_DirectLight* DLight, float Dist, float Angle, float Intensity, bool a_SkipRGBOnly )
{
	float result = 0.0f ;

	if( a_SkipRGBOnly && DLight->SunRGBOnly )
	{
		return 0.0f ;
	}

	switch( DLight->SunFallOffType )
	{
		default :
		case SUN_FALLOFF_NONE :
			result = Intensity * Angle ;
			break ;
		case SUN_FALLOFF_HARD_RADIUS :
			if( Dist > DLight->SunFallOffRadiusInTexels )
			{
				result = 0.0f ;
			}
			else
			{
				result = Intensity * Angle ;
			}			
			break ;
		case SUN_FALLOFF_GRADUALLY1:
		case SUN_FALLOFF_GRADUALLY2:
			if( Dist > DLight->SunFallOffRadiusInTexels )
			{
				// outside any radius
				result = 0.0f ;
			}
			if( Dist < DLight->SunFallOff100PercentRadius )
			{
				// inside the 100% radius
				result = Intensity * Angle ;
			}
			else
			{
				// somewhere between
				float r, dr, alpha ;
				dr =	Dist - DLight->SunFallOff100PercentRadius;
				r  =	DLight->SunFallOffRadiusInTexels - DLight->SunFallOff100PercentRadius ;
				alpha = DLight->SunFallOffAlpha ;

				if( DLight->SunFallOffType == SUN_FALLOFF_GRADUALLY1 )
				{
					//result = (( DLight->SunFallOffRadiusInTexels - Dist ) / DLight->SunFallOffRadiusInTexels ) * 
					result = 1.0f - (float) pow( dr / r, alpha ) ;
				}
				else
				{
					result = (float) pow( ( r - dr ) / r, alpha ) ;
				}
				result *= Intensity * Angle ;
			}			
			break ;
	}
	return result ;
}
// LWM: end

#if DEBUG_RWM_GOURAUD
static char MODULE_VertexFileName[ 512 ] = "test.v" ;
static void DebugWriteVertexSetFileName( const char* a_FileName )
{
	char* p;
	strcpy( MODULE_VertexFileName, a_FileName ) ;
	p = strrchr( MODULE_VertexFileName, '.' ) ;
	if( p )
	{
		strcpy( p, ".V" ) ;
	}
}
static void DebugWriteVertex( const geVec3d* a_Vert, const geVec3d* a_Normal, 
	const geVec3d* a_Color, int a_LightsCount, Light_DirectLight** a_LightsFound )
{
	static bool firstTime = true ;
	const char* name = MODULE_VertexFileName ;
	FILE* fp ;
	
	if (firstTime) {
		firstTime = false ;
		fp = fopen( name, "w" ) ;
		// write verion number
		fprintf( fp, "%f ", 1.0f ) ;
	}
	else {
		fp = fopen( name, "a" ) ;
	}

	if ( fp ) {
		geVec3d color ;

		fprintf( fp, "%f %f %f ", a_Vert->X, a_Vert->Y, a_Vert->Z ) ;

		// give the color the same treatment as FinalizeRGBVerts() would
		color = *a_Color ;
		FinalizeRGBVert( &color ) ;
		fprintf( fp, "%f %f %f ", color.X, color.Y, color.Z ) ;

		fprintf( fp, "%f %f %f ", a_Normal->X, a_Normal->Y, a_Normal->Z ) ;
		// write light count
		fprintf( fp, "%d ", a_LightsCount ) ;
		for( int i = 0 ; i < a_LightsCount ; i ++ )
		{
			// write light position
			fprintf( fp, "%f %f %f ", 
				a_LightsFound[ i ]->Origin.X, 
				a_LightsFound[ i ]->Origin.Y, 
				a_LightsFound[ i ]->Origin.Z ) ;
		}
		fprintf( fp, "\n" ) ;
		fclose( fp ) ;
		fp = NULL ;
	}
}
#endif


//====================================================================================
//	GouraudShadeFace
//====================================================================================
geBoolean GouraudShadeFace(int32 FaceNum)
{
	int32				NumVerts;
	Light_DirectLight	*DLight;
	GFX_Face			*pGFXFace;
	int32				v;
	GFX_TexInfo			*pGFXTexInfo;


	if (!GFXRGBVerts || !NumGFXRGBVerts)
	{
#if DEBUG_RWM_GOURAUD
		GHook.Printf("DON'T GouraudShadeFace()\n") ;
#endif
		return GE_FALSE;
	}

	pGFXFace = &GFXFaces[FaceNum];
	
	pGFXTexInfo = &GFXTexInfo[pGFXFace->TexInfo];

	NumVerts = pGFXFace->NumVerts;
	

	for (v=0; v< pGFXFace->NumVerts; v++)
	{
		int32		vn, Index, i;
		geVec3d		*pVert, Normal ;
		float		Dist, Angle, Val, Intensity;
#if DEBUG_RWM_GOURAUD
		int lightsCount ;
		#define MAX_LIGHTS 250
		Light_DirectLight* lightsFound[ MAX_LIGHTS ] ;

		lightsCount = 0 ;
#endif

		vn = pGFXFace->FirstVert+v;

		Index = GFXVertIndexList[vn];

		pVert = &GFXVerts[Index];

#if RWM_GOURAUD
		
		Normal = FaceInfo[FaceNum].Plane.Normal;
		
		#if DEBUG_RWM_GOURAUD
		/*
		if (pGFXTexInfo->Flags & TEXINFO_FLAT)
		{
			GHook.Printf("Found FLAT shaded face\n" ) ;
		}
		*/
		#endif

#else
		if (pGFXTexInfo->Flags & TEXINFO_FLAT)
			Normal = FaceInfo[FaceNum].Plane.Normal;
		else
			Normal = VertNormals[Index];
#endif	

		if( GFXTexInfo[GFXFaces[FaceNum].TexInfo].Flags & TEXINFO_FULLBRIGHT )
		{
			GFXRGBVerts[vn].X = 255 ;			
			GFXRGBVerts[vn].Y = 255 ;			
			GFXRGBVerts[vn].Z = 255 ;			
			#if DEBUG_RWM_GOURAUD
			DebugWriteVertex( pVert, &Normal, &GFXRGBVerts[vn], 0, NULL ) ;
			#endif
			continue ; // NOTE: (!)
		}


		for (i=0; i< NumDirectLights; i++)
		{
			geVec3d	Vect;

			DLight = DirectLights[i];

			Intensity = DLight->Intensity;

			// Find the angle between the light, and the face normal
			geVec3d_Subtract(&DLight->Origin, pVert, &Vect);
		
			Dist = geVec3d_Normalize (&Vect);

			Angle = geVec3d_DotProduct(&Vect, &Normal);

			Val = 0 ;

			// Wismerhill
			if (DLight->Type != DLight_SunLight && Angle <= 0.001f) 							
				goto Skip;
				
			switch(DLight->Type)
			{
			// Wismerhill
				case DLight_SunLight:
   				{
   					// Find the angle between the light normal, and the face normal
   					Angle = -geVec3d_DotProduct(&DLight->Normal, &Normal);
					Val = Intensity * Angle;
   					break;
   				}
   
				// LWM: start
				case DLight_Sun:
				{
#if 0 // #if RWM_GOURAUD
					float l_r = 1.25f * DLight->SunFallOffRadiusInTexels ;
					float l_v ;
					if ( Dist < l_r ) 
						l_v = 1.25f * ( l_r - Dist ) / l_r  ;
					else
						l_v = 0.0f ;
					Val = l_v * Intensity * Angle; //(Angle*0.5f+0.5f);
#else
					Val = 1.25f * SunCalcLightValue( DLight, Dist, Angle, Intensity, false ) ;
#endif
					break ;
				}
				// LWM: end
				case DLight_Point:
				{
					Val = (Intensity - Dist) * Angle;//(Angle*0.5f+0.5f);
					break;
				}
				case DLight_Spot:
				{
					float Angle2 = -geVec3d_DotProduct(&Vect, &DLight->Normal);

					if (Angle2 < DLight->Angle)
						goto Skip;

					Val = (Intensity - Dist) * Angle;
					break;
				}
				case DLight_Surface:
				{
					float Angle2 = -geVec3d_DotProduct (&Vect, &DLight->Normal);
					if (Angle2 <= 0.001f)
						goto Skip;						// Behind light surface

					Val = (Intensity / (Dist*Dist) ) * Angle * Angle2;
					break;
				}
				default:
				{
					GHook.Error("ApplyLightsToFace:  Invalid light.\n");
					return GE_FALSE;
				}
			}

			if (Val <= 0.0f)
				goto Skip;
			// Wismerhill
			if(DLight->Type == DLight_SunLight) 
			{
				geVec3d_AddScaled(pVert, &DLight->Normal, -20000.0f, &Vect);			//SMALL CORRECTION
  				if (RayCollisionButSky(pVert, &Vect, NULL))
  					goto Skip; // Ray is in shadow
			}
			else
			{
  				Vect = DLight->Origin;
  				if (RayCollision(pVert, &Vect, NULL))
  					goto Skip; // Ray is in shadow
			}
   
#if RWM_GOURAUD
			// allow some obstructions 
			{
				geVec3d n ; // normalized normal
				geVec3d test ; // point to test for visibility of the light
				geVec3d x, y, crossWith ; // to build x,y axis in plane
				int dist ; // counter
				#define NR_DISTANCES 4
				#define USE_NR_DISTANCES 3
				float texels[ NR_DISTANCES ] =      { 2.0f, 5.0f, 20.0f, 40.0f } ; // distance from plane
				float intensities[ NR_DISTANCES ] = { 1.0f, 0.9f,  0.7f,  0.5f } ; // intensityfactor
				float result = 0 ; // resulting Val
				int found = 0 ;

				n = Normal ; 
				geVec3d_Normalize( &n ) ;

				// see if Y-axis is OK for crossproduct
				crossWith.X = 0.0f ;
				crossWith.Y = 1.0f ;
				crossWith.Z = 0.0f ;
				if( geVec3d_Compare( &crossWith, &n, 0.1f ) )
				{
					// try Z-axis for crossproduct
					crossWith.X = 0.0f ;
					crossWith.Y = 0.0f ;
					crossWith.Z = 1.0f ;
				}

				// build X and Y axis inside plane
				geVec3d_CrossProduct( &n, &crossWith, &x ) ;
				geVec3d_Normalize( &x ) ;
				geVec3d_CrossProduct( &n, &x, &y ) ;
				geVec3d_Normalize( &y ) ;

				for( dist = 0 ; dist < USE_NR_DISTANCES ; dist ++ )
				{
					int	quadrant ;
					float XY_SCALE = texels[ dist ] / 2.0f ;

					for( quadrant = 0 ; quadrant < 5 ; quadrant ++ )
					{
						// create a test point somewhat removed from the face
						// and somewhat away from the direction the normal points in
						// to avoid getting inside planes nearby
						int sunQuadrant ;
						#define SUN_SIZE 32.0f

						geVec3d_AddScaled( pVert, &n, texels[ dist ], &test ) ;
						switch( quadrant )
						{
							case 0 :
								geVec3d_AddScaled( &test, &x, +1.0f * XY_SCALE, &test ) ;
								geVec3d_AddScaled( &test, &y, +1.0f * XY_SCALE, &test ) ;
								break ;
							case 1 :
								geVec3d_AddScaled( &test, &x, -1.0f * XY_SCALE, &test ) ;
								geVec3d_AddScaled( &test, &y, +1.0f * XY_SCALE, &test ) ;
								break ;
							case 2 :
								geVec3d_AddScaled( &test, &x, -1.0f * XY_SCALE, &test ) ;
								geVec3d_AddScaled( &test, &y, -1.0f * XY_SCALE, &test ) ;
								break ;
							case 3 :
								geVec3d_AddScaled( &test, &x, +1.0f * XY_SCALE, &test ) ;
								geVec3d_AddScaled( &test, &y, -1.0f * XY_SCALE, &test ) ;
								break ;
							case 4 :
								// straight up along normal
								break ;
						}
#if 1
						// just use the origin of the light
						for( sunQuadrant = 0 ; sunQuadrant < 1 ; sunQuadrant ++ )
#else
						// check some points around the light too
						for( sunQuadrant = 0 ; sunQuadrant < 9 ; sunQuadrant ++ )
#endif
						{
							// see if this testpoint can see the light
							geVec3d origin ;

							origin = DLight->Origin ;
							switch( sunQuadrant )
							{
								case 0 :
									// just take origin
									break ;
								case 1 :
								case 2 :
									origin.X -= SUN_SIZE ;
									origin.Z -= SUN_SIZE ;
									break ;
								case 3 :
								case 4 :
									origin.X += SUN_SIZE ;
									origin.Z -= SUN_SIZE ;
									break ;
								case 5 :
								case 6 :
									origin.X += SUN_SIZE ;
									origin.Z += SUN_SIZE ;
									break ;
								case 7 :
								case 8 :
									origin.X -= SUN_SIZE ;
									origin.Z += SUN_SIZE ;
									break ;
							}
							if( sunQuadrant > 0 )
							{
								if( sunQuadrant % 2 )
								{
									origin.Y -= SUN_SIZE ;
								}
								else
								{
									origin.Y += SUN_SIZE ;
								}
							}
							if (  (DLight->SunRGBAlwaysVisible)
							||    (! RayCollision(&test, &origin, NULL))
							) {
								// yes
								result = Val * intensities[ dist ] ;
								found = 1 ;
								break ;
							}
						}
						if( found )
						{
							break ;
						}
					}
					if( found )
					{
						break ;
					}
				}
				if( ! found )
				{
					goto Skip;
				}
				Val = result ;
			}
#else
			// This is the slowest test, so make it last
			if (RayCollision(pVert, &DLight->Origin, NULL))
				goto Skip;	// Ray is in shadow
#endif

			geVec3d_AddScaled(&GFXRGBVerts[vn], &DLight->Color, Val, &GFXRGBVerts[vn]);
			
			#if DEBUG_RWM_GOURAUD
			// remember light found
			if( lightsCount < MAX_LIGHTS )
			{
				lightsFound[ lightsCount ++ ] = DLight ;
			}
			#endif
			Skip:;
		
		}
		#if DEBUG_RWM_GOURAUD
		DebugWriteVertex( pVert, &Normal, &GFXRGBVerts[vn], lightsCount, lightsFound ) ;
		#endif
	}

	return GE_TRUE;
}

#pragma pack(1)

typedef struct
{
	float r, g, b;
} RGB;

#pragma pack()

//====================================================================================
//	ApplyLightsToFace
//====================================================================================
geBoolean ApplyLightsToFace(FInfo *FaceInfo, LInfo *LightInfo, float Scale)
{
	int32				c, v;
	geVec3d				*Verts;
	geFloat				Dist;
	int32				LType;
	geVec3d				*pRGBLData, Normal, Vect;
	float				Val, Angle;
	uint8				*VisData;
	int32				Leaf, Cluster;
	float				Intensity;
	Light_DirectLight	*DLight;

	Normal = FaceInfo->Plane.Normal;

	Verts = FaceInfo->Points;

	for (v=0; v< FaceInfo->NumPoints; v++)
	{
		Leaf = FindGFXLeaf(0, &Verts[v]);

		if (Leaf < 0 || Leaf >= NumGFXLeafs)
		{
			GHook.Error("ApplyLightsToFace:  Invalid leaf num.\n");
			return GE_FALSE;
		}

		Cluster = GFXLeafs[Leaf].Cluster;

		if (Cluster < 0)
			continue;

		if (Cluster >= NumGFXClusters)
		{
			GHook.Error("*WARNING* ApplyLightsToFace:  Invalid cluster num.\n");
			//return GE_FALSE;
			continue;
		}

		VisData = &GFXVisData[GFXClusters[Cluster].VisOfs];
		
		for (c=0; c< NumGFXClusters; c++)
		{
			if (!(VisData[c>>3] & (1<<(c&7))) )
				continue;

			for (DLight = DirectClusterLights[c]; DLight; DLight = DLight->Next)
			{
				Intensity = DLight->Intensity;
			
			#if 0
				geVec3d_Subtract(&FaceInfo->Center, &DLight->Origin, &Vect);
				Dist = geVec3d_Length(&Vect);
				if (Dist > Intensity+FaceInfo->Radius)
					continue;		// Can't possibly touch...
			#endif

				// Find the angle between the light, and the face normal
				geVec3d_Subtract(&DLight->Origin, &Verts[v], &Vect);
				Dist = geVec3d_Normalize(&Vect);

				Angle = geVec3d_DotProduct(&Vect, &Normal);
				// Wismerhill
				if (DLight->Type != DLight_SunLight && Angle <= 0.001f) //no skip    if it's a sun
							
					goto Skip;
				
				switch(DLight->Type)
				{
					// Wismerhill
					case DLight_SunLight:
   					{
		   				// Find the angle between the light normal, and the face normal
		   				Angle = -geVec3d_DotProduct(&DLight->Normal, &Normal);
		 				Val = Intensity * Angle;
		 				break;
   					}
					// LWM: start
					case DLight_Sun:
					{
						Val = SunCalcLightValue( DLight, Dist, Angle, Intensity, true ) ;
						break ;
					}
					// LWM: end
					case DLight_Point:
					{
						Val = (Intensity - Dist) * Angle;
						break;
					}
					case DLight_Spot:
					{
						float Angle2 = -geVec3d_DotProduct(&Vect, &DLight->Normal);

						if (Angle2 < DLight->Angle)
							goto Skip;

						Val = (Intensity - Dist) * Angle;
						break;
					}
					case DLight_Surface:
					{
						float Angle2 = -geVec3d_DotProduct (&Vect, &DLight->Normal);
						if (Angle2 <= 0.001f)
							goto Skip;						// Behind light surface

						Val = (Intensity / (Dist*Dist) ) * Angle * Angle2;
						break;
					}
					default:
					{
						GHook.Error("ApplyLightsToFace:  Invalid light.\n");
						return GE_FALSE;
					}
				}

				if (Val <= 0.0f)
					goto Skip;

				// Wismerhill
				// Test if the ray is in shadow with a collision test between a lighted point    and the light
				if(DLight->Type == DLight_SunLight)
				{
	 				//simulate a veeeeery far light
   					geVec3d_AddScaled(&Verts[v], &DLight->Normal, -20000.0f, &Vect);
   					if (RayCollisionButSky(&Verts[v], &Vect, NULL))
   						goto Skip; // Ray is in shadow
				} 
				else
				{
   					Vect = DLight->Origin;
   					if (RayCollision(&Verts[v], &Vect, NULL))
	   					goto Skip; // Ray is in shadow
				}

				LType = DLight->LType;

				// If the data for this LType has not been allocated, allocate it now...
				if (!LightInfo->RGBLData[LType])
				{
					if (LightInfo->NumLTypes >= MAX_LTYPES)
					{
						GHook.Error("Max Light Types on face.\n");
						return GE_FALSE;
					}		
				
					LightInfo->RGBLData[LType] = GE_RAM_ALLOCATE_ARRAY(geVec3d,FaceInfo->NumPoints);
					memset(LightInfo->RGBLData[LType], 0, FaceInfo->NumPoints*sizeof(geVec3d));
					LightInfo->NumLTypes++;
				}
		
				pRGBLData = LightInfo->RGBLData[LType];
				geVec3d_AddScaled(&pRGBLData[v], &DLight->Color, Val*Scale, &pRGBLData[v]);

				Skip:;
			}
		}
	}

	return GE_TRUE;
}

//====================================================================================
//	SaveLightmaps
//====================================================================================
geBoolean SaveLightmaps(geVFile *f)
{
	LInfo		*L;
	int32		i, j, k,l, Size;
	float		Max, Max2;
	GBSP_Chunk	Chunk;
	uint8		LData[MAX_LMAP_SIZE*MAX_LMAP_SIZE*3*4], *pLData;
	int32		Pos1, Pos2;
	int32		NumLTypes;
	FInfo		*pFaceInfo;

	geVFile_Tell(f, &Pos1);
	
	// Write out fake chunk (so we can write the real one here later)
	Chunk.Type = GBSP_CHUNK_LIGHTDATA;
	Chunk.Size = sizeof(uint8);
	Chunk.Elements = 0;

	if (!WriteChunk(&Chunk, NULL, f))
	{
		GHook.Error("SaveLightmaps:  Could not write Chunk Info.\n");
		return GE_FALSE;
	}

	// Reset the light offset
	LightOffset = 0;
	
	// Go through all the faces
	for (i=0; i< NumGFXFaces; i++)
	{
		L = &Lightmaps[i];
		pFaceInfo = &FaceInfo[i];
		
		// Set face defaults
		GFXFaces[i].LightOfs = -1;
		GFXFaces[i].LWidth = L->LSize[0]+1;
		GFXFaces[i].LHeight = L->LSize[1]+1;
		GFXFaces[i].LTypes[0] = 255;
		GFXFaces[i].LTypes[1] = 255;
		GFXFaces[i].LTypes[2] = 255;
		GFXFaces[i].LTypes[3] = 255;
		
		// Skip special faces with no lightmaps
		if (GFXTexInfo[GFXFaces[i].TexInfo].Flags & TEXINFO_NO_LIGHTMAP)
			continue;

		// Get the size of map
		Size = pFaceInfo->NumPoints;

		// Create style 0, if min light is set, and style 0 does not exist
		if (!L->RGBLData[0] && (MinLight.X > 1 || MinLight.Y > 1 || MinLight.Z > 1))
		{
			L->RGBLData[0] = GE_RAM_ALLOCATE_ARRAY(geVec3d,Size);
			if (!L->RGBLData[0])
			{
				GHook.Error("SaveLightmaps:  Out of memory for lightmap.\n");
				return GE_FALSE;
			}
			L->NumLTypes++;
			memset(L->RGBLData[0], 0, Size*sizeof(geVec3d));
		}
		
		// At this point, if no styles hit the face, skip it...
		if (!L->NumLTypes)
			continue;

		// Mark the start of the lightoffset
		GFXFaces[i].LightOfs = LightOffset;

		// At this point, all lightmaps are currently RGB
		uint8 RGB2 = 1;
		
		if (RGB2)
			RGBMaps++;
		else
			REGMaps++;

		if (geVFile_Write(f, &RGB2, sizeof(uint8)) != GE_TRUE)
		{
			GHook.Error("SaveLightMaps:  There was an error saving the Lightmap type.\n");
			return GE_FALSE;
		}

		LightOffset++;		// Skip the rgb light byte
		
		NumLTypes = 0;		// Reset number of LTypes for this face
		for (k=0; k< MAX_LTYPE_INDEX; k++)
		{
			if (!L->RGBLData[k])
				continue;

			if (NumLTypes >= MAX_LTYPES)
			{
				GHook.Error("SaveLightmaps:  Max LightTypes on face.\n");
				return GE_FALSE;
			}
				 
			GFXFaces[i].LTypes[NumLTypes] =  (uint8)k;
			NumLTypes++;

			pLData = LData;
			geVec3d *pRGB = L->RGBLData[k];

			for (j=0; j< Size; j++, pRGB++)
			{
				geVec3d		WorkRGB;

				geVec3d_Scale(pRGB, LightScale, &WorkRGB);

				if (k == 0)
					geVec3d_Add(&WorkRGB, &MinLight, &WorkRGB);
				
				Max = 0.0f;

				for (l=0; l<3; l++)
				{
					geFloat		Val;

					Val = geVec3d_GetElement(&WorkRGB, l);

					if (Val < 1.0f)
					{
						Val = 1.0f;
						VectorToSUB(WorkRGB, l) = Val;
					}

					if (Val > Max)
						Max = Val;
				}

				assert(Max > 0.0f);
				
				Max2 = min(Max, MaxLight);

				for (l=0; l<3; l++)
				{
					*pLData= (uint8)(geVec3d_GetElement(&WorkRGB, l)*Max2/Max);
					pLData++;
					LightOffset++;
				}
			}

			if (geVFile_Write(f, LData, 3 * Size) != GE_TRUE)
			{
				GHook.Error("There was an error saving the Lightmap data.\n");
				return GE_FALSE;
			}

			geRam_Free(L->RGBLData[k]);		// Free them as soon as we don't need them
			L->RGBLData[k] = NULL;
		}

		if (L->NumLTypes != NumLTypes)
		{
			GHook.Error("SaveLightMaps:  Num LightTypes was incorrectly calculated.\n");
			return GE_FALSE;
		}
	}

	GHook.Printf("Light Data Size      : %6i\n", LightOffset);

	geVFile_Tell(f, &Pos2);

	geVFile_Seek(f, Pos1, GE_VFILE_SEEKSET);
	
	Chunk.Type = GBSP_CHUNK_LIGHTDATA;
	Chunk.Size = sizeof(uint8);
	Chunk.Elements = LightOffset;

	if (!WriteChunk(&Chunk, NULL, f))
	{
		GHook.Error("SaveLightmaps:  Could not write Chunk Info.\n");
		return GE_FALSE;
	}

	geVFile_Seek(f, Pos2, GE_VFILE_SEEKSET);

	return GE_TRUE;
}

//====================================================================================
//	GetFacePlane
//====================================================================================
void GetFacePlane(int32 Face, GFX_Plane *Plane)
{
	Plane->Normal = GFXPlanes[GFXFaces[Face].PlaneNum].Normal;
	Plane->Dist =  GFXPlanes[GFXFaces[Face].PlaneNum].Dist;
	Plane->Type = GFXPlanes[GFXFaces[Face].PlaneNum].Type;

	if (GFXFaces[Face].PlaneSide)
	{
		geVec3d_Subtract(&VecOrigin, &Plane->Normal, &Plane->Normal);
		Plane->Dist = -Plane->Dist;
	}
}

//====================================================================================
//	CalcFaceInfo
//====================================================================================
geBoolean CalcFaceInfo(FInfo *FaceInfo, LInfo *LightInfo)
{
	int32		i, k;
	GFX_TexInfo	*TexInfo;
	geVec3d		*Vert;
	float		Val, Mins[2], Maxs[2];
	int32		Face = FaceInfo->Face;
	geVec3d		TexNormal;
	float		DistScale;
	geFloat		Dist, Len;
	int32		*pIndex;
	
	for (i=0; i<2; i++)
	{
		Mins[i] = MIN_MAX_BOUNDS;
		Maxs[i] =-MIN_MAX_BOUNDS;
	}

	TexInfo = &GFXTexInfo[GFXFaces[Face].TexInfo];

	geVec3d_Clear(&FaceInfo->Center);

	pIndex = &GFXVertIndexList[GFXFaces[Face].FirstVert];

	for (i=0; i< GFXFaces[Face].NumVerts; i++, pIndex++)
	{
		Vert = &GFXVerts[*pIndex];
		for (k=0; k< 2; k++)
		{
			Val = geVec3d_DotProduct(Vert, &TexInfo->Vecs[k]);

			if (Val > Maxs[k])
				Maxs[k] = Val;
			if (Val < Mins[k])
				Mins[k] = Val;
		}

		// Find center
		geVec3d_Add(&FaceInfo->Center, Vert, &FaceInfo->Center);
	}

	// Finish center
	geVec3d_Scale(&FaceInfo->Center, 1.0f/(float)GFXFaces[Face].NumVerts, &FaceInfo->Center);

#if 0
	// Find radius
	FaceInfo->Radius = 0.0f;
	pIndex = &GFXVertIndexList[GFXFaces[Face].FirstVert];
	for (i=0; i< GFXFaces[Face].NumVerts; i++, pIndex++)
	{
		geVec3d Vect;

		Vert = &GFXVerts[*pIndex];

		geVec3d_Subtract(&FaceInfo->Center, Vert, &Vect);

		Dist = geVec3d_Length(&Vect);

		if (Dist > FaceInfo->Radius)
			FaceInfo->Radius = Dist;
	}
#endif

	// Get the Texture U/V mins/max, and Grid aligned lmap mins/max/size
	for (i=0; i<2; i++)
	{
		LightInfo->Mins[i] = Mins[i];
		LightInfo->Maxs[i] = Maxs[i];

		Mins[i] = (float)floor(Mins[i]/LGRID_SIZE);
		Maxs[i] = (float)ceil(Maxs[i]/LGRID_SIZE);

		LightInfo->LMins[i] = (int32)Mins[i];
		LightInfo->LMaxs[i] = (int32)Maxs[i];
		LightInfo->LSize[i] = (int32)(Maxs[i] - Mins[i]);

		if (LightInfo->LSize[i]+1 > MAX_LMAP_SIZE)
		//if (LightInfo->LSize[i] > 17)
		{
			GHook.Error("CalcFaceInfo:  Face was not subdivided correctly.\n");
			return GE_FALSE;
		}
	}

	// Get the texture normal from the texture vecs
	geVec3d_CrossProduct(&TexInfo->Vecs[0], &TexInfo->Vecs[1], &TexNormal);
	// Normalize it
	geVec3d_Normalize(&TexNormal);
	
	// Flip it towards plane normal
	DistScale = geVec3d_DotProduct (&TexNormal, &FaceInfo->Plane.Normal);
	
	if (!DistScale)
	{
		GHook.Error ("CalcFaceInfo:  Invalid Texture vectors for face.\n");
		return GE_FALSE;
	}

	if (DistScale < 0)
	{
		DistScale = -DistScale;
		geVec3d_Inverse(&TexNormal);
	}	

	DistScale = 1/DistScale;

	// Get the tex to world vectors
	for (i=0 ; i<2 ; i++)
	{
		Len = geVec3d_Length(&TexInfo->Vecs[i]);
		Dist = geVec3d_DotProduct(&TexInfo->Vecs[i], &FaceInfo->Plane.Normal);
		Dist *= DistScale;
		geVec3d_AddScaled(&TexInfo->Vecs[i], &TexNormal, -Dist, &FaceInfo->T2WVecs[i]);
		geVec3d_Scale(&FaceInfo->T2WVecs[i], (1/Len)*(1/Len), &FaceInfo->T2WVecs[i]);
	}


	for (i=0 ; i<3 ; i++)
		VectorToSUB(FaceInfo->TexOrg,i) = 
			- TexInfo->Vecs[0].Z * VectorToSUB(FaceInfo->T2WVecs[0], i) 
		    - TexInfo->Vecs[1].Z * VectorToSUB(FaceInfo->T2WVecs[1], i);

	Dist = geVec3d_DotProduct (&FaceInfo->TexOrg, &FaceInfo->Plane.Normal) - FaceInfo->Plane.Dist - 1;
	Dist *= DistScale;
	geVec3d_AddScaled(&FaceInfo->TexOrg, &TexNormal, -Dist, &FaceInfo->TexOrg);

	return GE_TRUE;
}

//====================================================================================
//	CalcFacePoints
//====================================================================================
void CalcFacePoints(FInfo *FaceInfo, LInfo *LightInfo, float UOfs, float VOfs)
{
	geVec3d	*pPoint, FaceMid, I;
	float	MidU, MidV, StartU, StartV, CurU, CurV;
	int32	i, u, v, Width, Height, Leaf;
	geVec3d	Vect;
	uint8	InSolid[MAX_LMAP_SIZE*MAX_LMAP_SIZE], *pInSolid;

	MidU = (LightInfo->Maxs[0] + LightInfo->Mins[0])*0.5f;
	MidV = (LightInfo->Maxs[1] + LightInfo->Mins[1])*0.5f;

	for (i=0; i< 3; i++)
		VectorToSUB(FaceMid,i) = VectorToSUB(FaceInfo->TexOrg,i) + 
			VectorToSUB(FaceInfo->T2WVecs[0], i) * MidU + 
			VectorToSUB(FaceInfo->T2WVecs[1], i) * MidV;
	
	Width = (LightInfo->LSize[0]) + 1;
	Height = (LightInfo->LSize[1]) + 1;
	StartU = ((float)LightInfo->LMins[0]+UOfs) * (float)LGRID_SIZE;
	StartV = ((float)LightInfo->LMins[1]+VOfs) * (float)LGRID_SIZE;

	FaceInfo->NumPoints = Width*Height;

	pPoint = &FaceInfo->Points[0];
	pInSolid = InSolid;

	for (v=0; v < Height; v++)
	{
		for (u=0; u < Width; u++, pPoint++, pInSolid++)
		{
			CurU = StartU + u * LGRID_SIZE;
			CurV = StartV + v * LGRID_SIZE;
			
			for (i=0; i< 3; i++)
				VectorToSUB(*pPoint,i) = VectorToSUB(FaceInfo->TexOrg,i) + 
					VectorToSUB(FaceInfo->T2WVecs[0], i) * CurU + 
					VectorToSUB(FaceInfo->T2WVecs[1], i) * CurV;

			Leaf = FindGFXLeaf(0, pPoint);

			// Pre-compute if this point is in solid space, so we can re-use it in the code below
			if (GFXLeafs[Leaf].Contents & BSP_CONTENTS_SOLID2)
				*pInSolid = 1;
			else
				*pInSolid = 0;

			if (!ExtraLightCorrection)
			{
				if (*pInSolid)
				{
					if (RayCollision(&FaceMid, pPoint, &I))
					{
						geVec3d_Subtract(&FaceMid, pPoint, &Vect);
						geVec3d_Normalize(&Vect);
						geVec3d_Add(&I, &Vect, pPoint);
					}
				}
			}
		}
	}

	if (!ExtraLightCorrection)
		return;

	pPoint = FaceInfo->Points;
	pInSolid = InSolid;

	for (v=0; v< FaceInfo->NumPoints; v++, pPoint++, pInSolid++)
	{
		uint8		*pInSolid2;
		geVec3d		*pPoint2, *pBestPoint;
		geFloat		BestDist, Dist;

		if (!(*pInSolid))
			continue;						//  Point is good, leav it alone

		pPoint2 = FaceInfo->Points;
		pInSolid2 = InSolid;
		pBestPoint = &FaceMid;
		BestDist = MIN_MAX_BOUNDS;
		
		for (u=0; u< FaceInfo->NumPoints; u++, pPoint2++, pInSolid2++)
		{
			if (pPoint == pPoint2)			
				continue;					// We know this point is bad

			if (*pInSolid2)
				continue;					// We know this point is bad

			// At this point, we have a good point, now see if it's closer than the current good point
			geVec3d_Subtract(pPoint2, pPoint, &Vect);
			Dist = geVec3d_Length(&Vect);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				pBestPoint = pPoint2;

				if (Dist <= (LGRID_SIZE-0.1f))
					break;					// This should be good enough...
			}
		}

		*pPoint = *pBestPoint;
	}
}

float PlaneDistanceFast(geVec3d *Point, GFX_Plane *Plane)
{
   float	Dist,Dist2;
   Dist2 = Plane->Dist;

   switch (Plane->Type)
   {
	   
	   case PLANE_X:
           Dist = (Point->X - Dist2);
           break;
	   case PLANE_Y:
           Dist = (Point->Y - Dist2);
           break;
	   case PLANE_Z:
           Dist = (Point->Z - Dist2);
           break;
	      
       default:
           Dist = geVec3d_DotProduct(Point, &Plane->Normal) - Dist2;
           break;
    }

    return Dist;
}

static geBoolean HitLeaf;

//====================================================================================
//	RayIntersect
//====================================================================================
geBoolean RayIntersect(geVec3d *Front, geVec3d *Back, int32 Node)
{
    float	Fd, Bd, Dist;
    uint8	Side;
    geVec3d	I;

	if (Node < 0)						
	{
		int32 Leaf = -(Node+1);

		if (GFXLeafs[Leaf].Contents & BSP_CONTENTS_SOLID2)
			return GE_TRUE;					// Ray collided with solid space
		else 
			return GE_FALSE;					// Ray collided with empty space
	}

    Fd = PlaneDistanceFast(Front, &GFXPlanes[GFXNodes[Node].PlaneNum]);
    Bd = PlaneDistanceFast(Back , &GFXPlanes[GFXNodes[Node].PlaneNum]);

    if (Fd >= -1 && Bd >= -1) 
        return(RayIntersect(Front, Back, GFXNodes[Node].Children[0]));
    if (Fd < 1 && Bd < 1)
        return(RayIntersect(Front, Back, GFXNodes[Node].Children[1]));

    Side = Fd < 0;
    Dist = Fd / (Fd - Bd);

    I.X = Front->X + Dist * (Back->X - Front->X);
    I.Y = Front->Y + Dist * (Back->Y - Front->Y);
    I.Z = Front->Z + Dist * (Back->Z - Front->Z);

    // Work our way to the front, from the back side.  As soon as there
	// is no more collisions, we can assume that we have the front portion of the
	// ray that is in empty space.  Once we find this, and see that the back half is in
	// solid space, then we found the front intersection point...
	if (RayIntersect(Front, &I, GFXNodes[Node].Children[Side]))
        return GE_TRUE;
    else if (RayIntersect(&I, Back, GFXNodes[Node].Children[!Side]))
	{
		if (!HitLeaf)
		{
			GlobalPlane = GFXNodes[Node].PlaneNum;
			GlobalSide = Side;
			GlobalI = I;
			GlobalNode = Node;
			HitLeaf = GE_TRUE;
		}
		return GE_TRUE;
	}

	return GE_FALSE;
}

geBoolean RayCollision(geVec3d *Front, geVec3d *Back, geVec3d *I)
{
	HitLeaf = GE_FALSE;
	if (RayIntersect(Front, Back, GFXModels[0].RootNode[0]))
	{
		if (I) 
			*I = GlobalI;			// Set the intersection point
		return GE_TRUE;
	}

	return GE_FALSE;
}

// Wismerhill
#define COLLISION_BOX 1.0f
geBoolean RayCollisionButSky(geVec3d *Front, geVec3d *Back, geVec3d *I)
{
   HitLeaf = GE_FALSE;
   if (RayIntersect(Front, Back, GFXModels[0].RootNode[0]))
   {
	   if(HitLeaf)
		{
			GFX_Node *pNode;
		   	GFX_Face *pFace;
		   	int32 i, k, v, *pIndex;
		   	geVec3d VMins, VMaxs;
		   	geVec3d Vert;
	 		pNode = &GFXNodes[GlobalNode];
		   	pFace = &GFXFaces[pNode->FirstFace];
	 		//this code retrieves the face that we hit
		   	//by calculating its coords and testing if the impact point is in
		   	for(i=0;i<pNode->NumFaces;i++,pFace++)
		   	{
		   		//dummy mins/maxs
		   		for (k=0; k<3; k++)
		   		{
		   			VectorToSUB(VMins, k) = 99999.0f;
		   			VectorToSUB(VMaxs, k) =-99999.0f;
		   		}
				//points into the list of index for this face
		   		pIndex = &GFXVertIndexList[pFace->FirstVert];
	 			//loop through vertices of the face
		   		for (v=0; v < pFace->NumVerts; v++, pIndex++)
		   		{
		   			//extract the vertex vector
		   			Vert = GFXVerts[*pIndex];
					//compute mins/maxs
		   			for (k=0; k<3; k++)
		   			{
		   				if (VectorToSUB(Vert, k) < VectorToSUB(VMins, k))
		   					VectorToSUB(VMins, k) = VectorToSUB(Vert, k);
		   				if (VectorToSUB(Vert, k) > VectorToSUB(VMaxs, k))
		   					VectorToSUB(VMaxs, k) = VectorToSUB(Vert, k);
		   			}
		   		}
	 			//Mins & Maxs are calculated for this face
		   		//is the Impact on it?
		   		if (GlobalI.X + COLLISION_BOX >= VMins.X && GlobalI.X - COLLISION_BOX    <= VMaxs.X)
		   		if (GlobalI.Y + COLLISION_BOX >= VMins.Y && GlobalI.Y - COLLISION_BOX    <= VMaxs.Y)
		   		if (GlobalI.Z + COLLISION_BOX >= VMins.Z && GlobalI.Z - COLLISION_BOX    <= VMaxs.Z)
		   		{ //yes! It's our face 
		   			if(GFXTexInfo[pFace->TexInfo].Flags & TEXINFO_SKY) { //is it face marked    sky?
		   				return GE_FALSE; //undo the collision: sunlight must pass
		   			}
		   		}
	   		}
		}

		if (I) 
	   		*I = GlobalI; // Set the intersection point
   	return GE_TRUE;
  	}
return GE_FALSE;
}

//================================================================================
//	StartWriting
//================================================================================
geBoolean StartWriting(geVFile *f)
{
	// Write out everything but the light data
	// Don't include LIGHT_DATA since it was allready saved out...

	GBSP_ChunkData	CurrentChunkData[] = {
		{ GBSP_CHUNK_HEADER			, sizeof(GBSP_Header)	,1				, &GBSPHeader},
		{ GBSP_CHUNK_MODELS			, sizeof(GFX_Model)		,NumGFXModels	, GFXModels },
		{ GBSP_CHUNK_NODES			, sizeof(GFX_Node)		,NumGFXNodes	, GFXNodes  },
		{ GBSP_CHUNK_PORTALS		, sizeof(GFX_Portal)	,NumGFXPortals	, GFXPortals},
		{ GBSP_CHUNK_BNODES			, sizeof(GFX_BNode)		,NumGFXBNodes	, GFXBNodes },
		{ GBSP_CHUNK_LEAFS			, sizeof(GFX_Leaf)		,NumGFXLeafs	, GFXLeafs  },
		{ GBSP_CHUNK_AREAS			, sizeof(GFX_Area)		,NumGFXAreas	  , GFXAreas  },
		{ GBSP_CHUNK_AREA_PORTALS	, sizeof(GFX_AreaPortal),NumGFXAreaPortals	, GFXAreaPortals  },
		{ GBSP_CHUNK_CLUSTERS		, sizeof(GFX_Cluster)	,NumGFXClusters	, GFXClusters},
		{ GBSP_CHUNK_PLANES			, sizeof(GFX_Plane)		,NumGFXPlanes	, GFXPlanes },
		{ GBSP_CHUNK_LEAF_FACES		, sizeof(int32)			,NumGFXLeafFaces, GFXLeafFaces  },
		{ GBSP_CHUNK_LEAF_SIDES		, sizeof(GFX_LeafSide)	,NumGFXLeafSides, GFXLeafSides  },
		{ GBSP_CHUNK_VERTS			, sizeof(geVec3d)		,NumGFXVerts	, GFXVerts  },
		{ GBSP_CHUNK_VERT_INDEX		, sizeof(int32)			,NumGFXVertIndexList , GFXVertIndexList},
		{ GBSP_CHUNK_ENTDATA		, sizeof(uint8)			,NumGFXEntData	, GFXEntData},
		{ GBSP_CHUNK_TEXTURES		, sizeof(GFX_Texture)	,NumGFXTextures	, GFXTextures},
		{ GBSP_CHUNK_TEXINFO		, sizeof(GFX_TexInfo)	,NumGFXTexInfo	, GFXTexInfo},
		{ GBSP_CHUNK_TEXDATA		, sizeof(uint8)			,NumGFXTexData	, GFXTexData},
		{ GBSP_CHUNK_VISDATA		, sizeof(uint8)			,NumGFXVisData	, GFXVisData},
		{ GBSP_CHUNK_SKYDATA		, sizeof(GFX_SkyData)	,1				, &GFXSkyData},
		{ GBSP_CHUNK_PALETTES		, sizeof(DRV_Palette)	,NumGFXPalettes	, GFXPalettes},
		{ GBSP_CHUNK_MOTIONS		, sizeof(uint8)			,NumGFXMotionBytes, GFXMotionData},
	};

	if (!WriteChunks(CurrentChunkData, sizeof(CurrentChunkData) / sizeof(CurrentChunkData[0]), f))
	{
		GHook.Error("StartWriting:  Could not write ChunkData.\n");
		return GE_FALSE;
	}

	return GE_TRUE;
}

//================================================================================
//	FinishWriting
//================================================================================
geBoolean FinishWriting(geVFile *f)
{
	GBSP_ChunkData	ChunkDataEnd[] = {
		{ GBSP_CHUNK_RGB_VERTS	, sizeof(geVec3d)		,NumGFXRGBVerts	, GFXRGBVerts },
		{ GBSP_CHUNK_FACES		, sizeof(GFX_Face)		,NumGFXFaces	, GFXFaces  },
		{ GBSP_CHUNK_END		, 0						, 0				, NULL},
	};

	if (!WriteChunks(ChunkDataEnd, 3, f))
	{
		GHook.Error("FinishWriting:  Could not write ChunkData.\n");
		return GE_FALSE;
	}

	return GE_TRUE;
}

//================================================================================
//	FindGFXLeaf
//================================================================================
int32 FindGFXLeaf(int32 Node, geVec3d *Vert)
{
    float Dist;

    while (Node >= 0) 
	{
		Dist = PlaneDistanceFast(Vert, &GFXPlanes[GFXNodes[Node].PlaneNum]);
        if (Dist < 0) 
            Node = GFXNodes[Node].Children[1];
		else
            Node = GFXNodes[Node].Children[0];
    }
	
	return (-Node-1);
}

//================================================================================
//	FreeLightmaps
//================================================================================
void FreeLightmaps(void)
{
	int32		i, k;

	for (i=0; i< NumGFXFaces; i++)
	{
		if (FaceInfo)
		{
			if (FaceInfo[i].Points)
				geRam_Free(FaceInfo[i].Points);
			FaceInfo[i].Points = NULL;
		}

		if (Lightmaps)
		{
			for (k=0; k< MAX_LTYPE_INDEX; k++)
			{
				if (Lightmaps[i].RGBLData[k])
					geRam_Free(Lightmaps[i].RGBLData[k]);
				Lightmaps[i].RGBLData[k] = NULL;
			}
		}
	}

	if (FaceInfo)
		geRam_Free(FaceInfo);
	if (Lightmaps)
		geRam_Free(Lightmaps);

	FaceInfo = NULL;
	Lightmaps = NULL;
}

//========================================================================================
//	AllocDirectLight
//========================================================================================
Light_DirectLight *AllocDirectLight(void)
{
	Light_DirectLight	*DLight;

	DLight = GE_RAM_ALLOCATE_STRUCT(Light_DirectLight);

	if (!DLight)
	{
		GHook.Error("AllocDirectLight:  Could not create direct light.\n");
		return NULL;
	}

	memset(DLight, 0, sizeof(Light_DirectLight));

	return DLight;
}

//========================================================================================
//	FreeDirectLight
//========================================================================================
void FreeDirectLight(Light_DirectLight *DLight)
{
	if (!DLight)
	{
		GHook.Printf("*WARNING* FreeDirectLight:  NULL Light.\n");
		return;
	}

	geRam_Free(DLight);
}

geBoolean StringContains( const char* a_String, const char* a_SubStr )
{
	char stringBuf[ 512 ];
	char subStrBuf[ 512 ];
	memset( stringBuf, 0, sizeof( stringBuf ) ) ;
	memset( subStrBuf, 0, sizeof( subStrBuf ) ) ;
	strncpy( stringBuf, a_String, sizeof( stringBuf ) ) ;
	strncpy( subStrBuf, a_SubStr, sizeof( subStrBuf ) ) ;
	strupr( stringBuf ) ; 
	strupr( subStrBuf ) ;
	return strstr( stringBuf, subStrBuf ) != NULL ;
}

//========================================================================================
//	CreateDirectLights
//========================================================================================
geBoolean CreateDirectLights(void)
{
	int32				i, Leaf, Cluster;
	geVec3d				Color;
	MAP_Entity			*Entity;
	Light_DirectLight	*DLight;
	RAD_Patch			*Patch;
	geVec3d				Angles;
	geVec3d				Angles2;
	geXForm3d			XForm;
	GFX_TexInfo			*pTexInfo;
	int32				NumSurfLights;
	int					rgbOnlyCount = 0 ;
	int					rgbAlwaysVisibleCount = 0 ;
	int					SunsWithoutRadiusCount = 0 ;

	NumDirectLights = 0;
	NumSurfLights = 0;

	for (i=0; i< MAX_DIRECT_CLUSTER_LIGHTS; i++)
		DirectClusterLights[i] = NULL;

	// Create the entity lights first
	for (i=0; i< NumEntities; i++)
	{
		Entity = &Entities[i];

		if (!Entity->Light)
			continue;

		if (NumDirectLights+1 >= MAX_DIRECT_LIGHTS)
		{
			GHook.Printf("*WARNING* Max lights.\n");
			goto Done;
		}

		DLight = AllocDirectLight();

		if (!DLight)
			return GE_FALSE;

		GetColorForKey (Entity, "Color", &Color);

		// Default it to 255/255/255 if no light is specified
		if (!Color.X && !Color.Y && !Color.Z)
		{
			Color.X = 1.0f;
			Color.Y = 1.0f;
			Color.Z = 1.0f;
		}
		else
			ColorNormalize(&Color, &Color);

		DLight->Origin = Entity->Origin;
		DLight->Color = Color;
		DLight->Intensity = (float)Entity->Light * EntityScale;
		DLight->LType = Entity->LType;

		if (GetVectorForKey2 (Entity, "Angles", &Angles))
		{
			Angles2.X = (Angles.X / (geFloat)180) * GE_PI;
			Angles2.Y = (Angles.Y / (geFloat)180) * GE_PI;
			Angles2.Z = (Angles.Z / (geFloat)180) * GE_PI;

			geXForm3d_SetEulerAngles(&XForm, &Angles2);

			geXForm3d_GetLeft(&XForm, &Angles2);
			DLight->Normal.X = -Angles2.X;
			DLight->Normal.Y = -Angles2.Y;
			DLight->Normal.Z = -Angles2.Z;

			DLight->Angle = FloatForKey(Entity, "Arc");
			DLight->Angle = (float)cos(DLight->Angle/180.0f*GE_PI);
			
		}

		// Find out what type of light it is by it's classname...
		if (!stricmp(Entity->ClassName, "Light"))
			DLight->Type = DLight_Point;
		else if (!stricmp(Entity->ClassName, "SpotLight"))
			DLight->Type = DLight_Spot;
		// Wismerhill
		else if (!stricmp(Entity->ClassName, "SunLight"))
			DLight->Type = DLight_SunLight;
		// LWM: start
		else if (!stricmp(Entity->ClassName, "Sun"))
		{
			char* name ;

			DLight->Type = DLight_Sun;

			// ================== DaviName

			DLight->SunRGBOnly = false ;
			if( strcmpi( ValueForKey( Entity, name = "DaviName" ), "" ) )
			{				
				char* daviName = ValueForKey( Entity, name ) ;

				if( StringContains( daviName, "rgb" ) )
				{
					DLight->SunRGBOnly = true ;
					rgbOnlyCount ++ ;
				}
			}

			DLight->SunRGBAlwaysVisible = false ;
			if( (DLight->SunRGBOnly)
			&&  (strcmpi( ValueForKey( Entity, name = "DaviName" ), "" ))
			) {				
				char* daviName = ValueForKey( Entity, name ) ;
				if( StringContains( daviName, "visible" ) )
				{
					DLight->SunRGBAlwaysVisible = true ;
					rgbAlwaysVisibleCount ++ ;
				}
			}

			if( DLight->SunFallOffRadiusInTexels < 0.0f )
			{
				GHook.Printf("*WARNING* Invalid Sun %s %f.\n", 
								(const char*) name,
					            (float) DLight->SunFallOffRadiusInTexels 
				);
				DLight->SunFallOffRadiusInTexels = 32.0f ;
			}
			// ================== FallOffType
			
			// test if entity has "FallOffType" field
			if( strcmp( ValueForKey( Entity, name = "FallOffType" ), "" ) )
			{
				// let's hope it is an integer...
				DLight->SunFallOffType = atoi( ValueForKey( Entity, name ) ) ;
				if( DLight->SunFallOffType < SUN_MIN_FALLOFF_TYPE 
				||  DLight->SunFallOffType > SUN_MAX_FALLOFF_TYPE 
				) {
					GHook.Printf("*WARNING* Invalid Sun %s %d.\n", 
							(const char*) name,
						    (int) DLight->SunFallOffType 
					);
				}
				if( DLight->SunFallOffType==SUN_FALLOFF_NONE )
				{
					SunsWithoutRadiusCount ++ ;
				}
			}
			else
			{
				// field doesn't exist
				GHook.Printf("*WARNING* Sun has no %s.\n", 
					(const char*) name
				);
				DLight->SunFallOffType = SUN_MIN_FALLOFF_TYPE ;
			}

			// ================== FallOffRadiusInTexels

			if( strcmpi( ValueForKey( Entity, name = "FallOffRadiusInTexels" ), "" ) )
			{				
				// let's hope it is a float
				DLight->SunFallOffRadiusInTexels = FloatForKey( Entity, name ) ;
			}
			else
			{
				// field doesn't exist
				GHook.Printf("*WARNING* Sun has no %s.\n", 
					(const char*) name
				);
				DLight->SunFallOffRadiusInTexels = 32.0f ;
			}
			if( DLight->SunFallOffRadiusInTexels < 0.0f )
			{
				GHook.Printf("*WARNING* Invalid Sun %s %f.\n", 
								(const char*) name,
					            (float) DLight->SunFallOffRadiusInTexels 
				);
				DLight->SunFallOffRadiusInTexels = 32.0f ;
			}
			
			
			// ================== FallOffRadiusTopIntensity

			if( strcmpi( ValueForKey( Entity, name = "FallOffRadiusTopIntensity" ), "" ) )
			{				
				// let's hope it is a float
				DLight->SunFallOff100PercentRadius = FloatForKey( Entity, name ) ;

			}
			else
			{
				// field doesn't exist
				GHook.Printf("*WARNING* Sun has no %s.\n", 
					(const char*) name
				);
				DLight->SunFallOff100PercentRadius = 0 ;
			}
			if( DLight->SunFallOff100PercentRadius < 0.0f
			||  DLight->SunFallOff100PercentRadius > DLight->SunFallOffRadiusInTexels
			) {
				GHook.Printf("*WARNING* Invalid Sun %s %f.\n", 
					(const char*) name,
					(float) DLight->SunFallOff100PercentRadius
				);
				DLight->SunFallOff100PercentRadius = 0 ;
			}

			// ================== FallOffSecretNumber

			if( strcmpi( ValueForKey( Entity, name = "FallOffSecretNumber" ), "" ) )
			{				
				// let's hope it is a float
				DLight->SunFallOffAlpha = FloatForKey( Entity, name ) ;

			}
			else
			{
				// field doesn't exist
				GHook.Printf("*WARNING* Sun has no %s.\n", 
					(const char*) name
				);
				DLight->SunFallOffAlpha = 1.0f ;
			}
			
			if( DLight->SunFallOffAlpha < 0.0f 
			||  DLight->SunFallOffAlpha > 100.0f )
			{
				GHook.Printf("*ERROR* Invalid Sun %s %f.\n", 
					(const char*) name,
					(float) DLight->SunFallOffAlpha
				);
				DLight->SunFallOffAlpha = 1.0f ;
			}


#if DEBUG_SUN
			GHook.Printf("*DEBUG* Sun SunFallOffType %d.\n", (int) DLight->SunFallOffType );
			GHook.Printf("*DEBUG* Sun SunFallOffRadiusInTexels %f.\n", (float) DLight->SunFallOffRadiusInTexels );
			GHook.Printf("*DEBUG* Sun SunFallOff100PercentRadius %f.\n", (float) DLight->SunFallOff100PercentRadius );
			GHook.Printf("*DEBUG* Sun SunFallOffAlpha %f.\n", (float) DLight->SunFallOffAlpha );
#endif
		}
		// LWM: end
			

		Leaf = FindGFXLeaf(0, &Entity->Origin);
		Cluster = GFXLeafs[Leaf].Cluster;

		if (Cluster < 0)
		{
			GHook.Printf("*WARNING* CreateLights:  Light in solid leaf.\n");
			continue;
		}
		
		if (Cluster >= MAX_DIRECT_CLUSTER_LIGHTS)
		{
			GHook.Printf("*WARNING* CreateLights:  Max cluster for light.\n");
			continue;
		}

		DLight->Next = DirectClusterLights[Cluster];
		DirectClusterLights[Cluster] = DLight;

		DirectLights[NumDirectLights++] = DLight;
				
	}

	GHook.Printf("Num Normal Lights   : %5i\n", NumDirectLights);
	GHook.Printf("Num RGB only Lights   : %5i\n", rgbOnlyCount );
	GHook.Printf("Num RGB Always Visible Lights   : %5i\n", rgbAlwaysVisibleCount );
	GHook.Printf("Num Suns with fallofftype 0 : %5i\n", SunsWithoutRadiusCount ) ;

	if (!DoRadiosity)		// Stop here if no radisosity is going to be done
		return GE_TRUE;
	
	// Now create the radiosity direct lights (surface emitters)
	for (i=0; i< NumGFXFaces; i++)
	{
		pTexInfo = &GFXTexInfo[GFXFaces[i].TexInfo];

		// Only look at surfaces that want to emit light
		if (!(pTexInfo->Flags & TEXINFO_LIGHT))
			continue;

		for (Patch = FacePatches[i]; Patch; Patch = Patch->Next)
		{
			Leaf = Patch->Leaf;
			Cluster = GFXLeafs[Leaf].Cluster;

			if (Cluster < 0)
				continue;			// Skip, solid

			if (Cluster >= MAX_DIRECT_CLUSTER_LIGHTS)
			{
				GHook.Printf("*WARNING* CreateLights:  Max cluster for surface light.\n");
				continue;
			}

			if (NumDirectLights+1 >= MAX_DIRECT_LIGHTS)
			{
				GHook.Printf("*WARNING* Max lights.\n");
				goto Done;
			}

			DLight = AllocDirectLight();

			if (!DLight)
				return GE_FALSE;

			DLight->Origin = Patch->Origin;
			DLight->Color = Patch->Reflectivity;

			DLight->Normal = Patch->Plane.Normal;
			DLight->Type = DLight_Surface;
			
			DLight->Intensity = pTexInfo->FaceLight * Patch->Area;
			// Make sure the emitter ends up with some light too
			geVec3d_AddScaled(&Patch->RadFinal, &Patch->Reflectivity, DLight->Intensity, &Patch->RadFinal);

			// Insert this surface direct light into the list of lights
			DLight->Next = DirectClusterLights[Cluster];
			DirectClusterLights[Cluster] = DLight;

			DirectLights[NumDirectLights++] = DLight;
			NumSurfLights++;
		}
	}

	Done:

	GHook.Printf("Num Surf Lights     : %5i\n", NumSurfLights);

	return GE_TRUE;
}

//========================================================================================
//	FreeDirectLights
//========================================================================================
void FreeDirectLights(void)
{
	int32				i;

	for (i=0; i< MAX_DIRECT_LIGHTS; i++)
	{
		if (DirectLights[i])
			FreeDirectLight(DirectLights[i]);

		DirectLights[i] = NULL;
	}

	for (i=0; i< MAX_DIRECT_CLUSTER_LIGHTS; i++)
	{
		DirectClusterLights[i] = NULL;
	}

	NumDirectLights = 0;
}

