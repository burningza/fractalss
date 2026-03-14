//-----------------------------------------------------------------------------
// Torque Game Engine Advanced
// Written by Melvyn May, Started on 4th August 2002.
//
// "My code is written for the Torque community, so do your worst with it,
//	just don't rip-it-off and call it your own without even thanking me".
//
//	- Melv.
//
//
// Conversion to TSE By Brian "bzztbomb" Richardson 9/2005
//   This was a neat piece of code!  Thanks Melv!
//   I've switched this to use one large indexed primitive buffer.  All animation
//   is then done in the vertex shader.  This means we have a static vertex/primitive
//   buffer that never changes!  How spiff!  Because of this, the culling code was
//   changed to render out full quadtree nodes, we don't try to cull each individual
//   node ourselves anymore.  This means to get good performance, you probably need to do the 
//   following:
//     1.  If it's a small area to cover, turn off culling completely.
//     2.  You want to tune the parameters to make sure there are a lot of billboards within
//         each quadrant.
// 
// POTENTIAL TODO LIST:
//   TODO: Clamp item alpha to fog alpha
//-----------------------------------------------------------------------------

#include "platform/platform.h"
#include "gfx/gfxDevice.h"
#include "gfx/PrimBuilder.h"	// Used for debug / mission edit rendering
#include "console/consoleTypes.h"
#include "core/bitStream.h"
#include "math/mRandom.h"
#include "math/mathIO.h"
#include "terrain/terrData.h"
#include "console/simBase.h"
#include "sceneGraph/sceneGraph.h"
#include "T3D/fx/fxFractals.h"
#include "renderInstance/renderInstMgr.h"

#pragma warning( push, 4 )
// I just could not get rid of this warning without flat out disabling it. ;/
// If you know how, I'm very curious, email me at bzzt@knowhere.net
// thanks
#pragma warning( disable : 4127 )

const U32 AlphaTexLen = 1024;

//------------------------------------------------------------------------------
//
//	Put the function in /example/common/editor/ObjectBuilderGui.gui [around line 458] ...
//
//	function ObjectBuilderGui::buildfxFractalItemReplicator(%this)
//	{
//		%this.className = "fxFractalItemReplicator";
//		%this.process();
//	}
//
//------------------------------------------------------------------------------
//
//	Put this in /example/common/editor/EditorGui.cs in [function Creator::init( %this )]
//	
//   %Environment_Item[8] = "fxFractalItemReplicator";  <-- ADD THIS.
//
//------------------------------------------------------------------------------
//
//	Put this in /example/common/client/missionDownload.cs in [function clientCmdMissionStartPhase3(%seq,%missionName)] (line 65)
//	after codeline 'onPhase2Complete();'.
//
//	StartFoliageReplication();
//
//------------------------------------------------------------------------------
//
//	Put this in /engine/console/simBase.h (around line 509) in
//
//	namespace Sim
//  {
//	   DeclareNamedSet(fxFractalSet)  <-- ADD THIS (Note no semi-colon).
//
//------------------------------------------------------------------------------
//
//	Put this in /engine/console/simBase.cc (around line 19) in
//
//  ImplementNamedSet(fxFractalSet)  <-- ADD THIS (Note no semi-colon).
//
//------------------------------------------------------------------------------
//
//	Put this in /engine/console/simManager.cc [function void init()] (around line 269).
//
//	namespace Sim
//  {
//		InstantiateNamedSet(fxFractalSet);  <-- ADD THIS (Including Semi-colon).
//
//------------------------------------------------------------------------------
extern bool gEditingMission;

//------------------------------------------------------------------------------

IMPLEMENT_CO_NETOBJECT_V1(fxFractalItemReplicator);


//------------------------------------------------------------------------------
//
// Trig Table Lookups.
//
//------------------------------------------------------------------------------
const F32 PeriodLen = (F32) 2.0f * (F32) M_PI;
const F32 PeriodLenMinus = (F32) (2.0f * M_PI) - 0.01f;

//------------------------------------------------------------------------------
//
// Class: fxFractalRenderList
//
//------------------------------------------------------------------------------

void fxFractalRenderList::SetupClipPlanes(SceneState* state, const F32 FarClipPlane)
{
	// Fetch Camera Position.
	CameraPosition  = state->getCameraPosition();
	// Calculate Perspective.
	F32 FarOverNear = FarClipPlane / (F32) state->getNearPlane();

	// Calculate Clip-Planes.
	FarPosLeftUp    = Point3F(	(F32) state->getBaseZoneState().frustum[0] * FarOverNear,
								FarClipPlane,
								(F32) state->getBaseZoneState().frustum[3] * FarOverNear);
	FarPosLeftDown  = Point3F(	(F32) state->getBaseZoneState().frustum[0] * FarOverNear,
								FarClipPlane,
								(F32) state->getBaseZoneState().frustum[2] * FarOverNear);
	FarPosRightUp   = Point3F(	(F32) state->getBaseZoneState().frustum[1] * FarOverNear,
								FarClipPlane,
								(F32) state->getBaseZoneState().frustum[3] * FarOverNear);
	FarPosRightDown = Point3F(	(F32) state->getBaseZoneState().frustum[1] * FarOverNear,
								FarClipPlane,
								(F32) state->getBaseZoneState().frustum[2] * FarOverNear);

	// Calculate our World->Object Space Transform.
	MatrixF InvXForm = state->mModelview;
	InvXForm.inverse();
	// Convert to Object-Space.
	InvXForm.mulP(FarPosLeftUp);
	InvXForm.mulP(FarPosLeftDown);
	InvXForm.mulP(FarPosRightUp);
	InvXForm.mulP(FarPosRightDown);

	// Calculate Bounding Box (including Camera).
	mBox.min = CameraPosition;
	mBox.min.setMin(FarPosLeftUp);
	mBox.min.setMin(FarPosLeftDown);
	mBox.min.setMin(FarPosRightUp);
	mBox.min.setMin(FarPosRightDown);
	mBox.max = CameraPosition;
	mBox.max.setMax(FarPosLeftUp);
	mBox.max.setMax(FarPosLeftDown);
	mBox.max.setMax(FarPosRightUp);
	mBox.max.setMax(FarPosRightDown);

	// Setup Our Viewplane.
	ViewPlanes[0].set(CameraPosition,	FarPosLeftUp,		FarPosLeftDown);
	ViewPlanes[1].set(CameraPosition,	FarPosRightUp,		FarPosLeftUp);
	ViewPlanes[2].set(CameraPosition,	FarPosRightDown,	FarPosRightUp);
	ViewPlanes[3].set(CameraPosition,	FarPosLeftDown,		FarPosRightDown);
	ViewPlanes[4].set(FarPosLeftUp,		FarPosRightUp,		FarPosRightDown);
}

//------------------------------------------------------------------------------


inline void fxFractalRenderList::DrawQuadBox(const Box3F& QuadBox, const ColorF Colour)
{
	// Define our debug box.
	static Point3F BoxPnts[] = {
								  Point3F(0,0,0),
								  Point3F(0,0,1),
								  Point3F(0,1,0),
								  Point3F(0,1,1),
								  Point3F(1,0,0),
								  Point3F(1,0,1),
								  Point3F(1,1,0),
								  Point3F(1,1,1)
								};

	static U32 BoxVerts[][4] = {
								  {0,2,3,1},     // -x
								  {7,6,4,5},     // +x
								  {0,1,5,4},     // -y
								  {3,2,6,7},     // +y
								  {0,4,6,2},     // -z
								  {3,7,5,1}      // +z
								};

	static Point3F BoxNormals[] = {
								  Point3F(-1, 0, 0),
								  Point3F( 1, 0, 0),
								  Point3F( 0,-1, 0),
								  Point3F( 0, 1, 0),
								  Point3F( 0, 0,-1),
								  Point3F( 0, 0, 1)
								};

	// Project our Box Points.
	Point3F ProjectionPoints[8];

   for( U32 i=0; i<8; i++ )
	{
		ProjectionPoints[i].set(BoxPnts[i].x ? QuadBox.max.x : QuadBox.min.x,
								BoxPnts[i].y ? QuadBox.max.y : QuadBox.min.y,
								BoxPnts[i].z ? (mHeightLerp * QuadBox.max.z) + (1-mHeightLerp) * QuadBox.min.z : QuadBox.min.z);

	}

	PrimBuild::color(Colour);

	// Draw the Box.
	for(U32 x = 0; x < 6; x++)
	{
		// Draw a line-loop.
		PrimBuild::begin(GFXLineStrip, 5);

		for(U32 y = 0; y < 4; y++)
		{
			PrimBuild::vertex3f(ProjectionPoints[BoxVerts[x][y]].x,
						ProjectionPoints[BoxVerts[x][y]].y,
						ProjectionPoints[BoxVerts[x][y]].z);
		}
		PrimBuild::vertex3f(ProjectionPoints[BoxVerts[x][0]].x,
					ProjectionPoints[BoxVerts[x][0]].y,
					ProjectionPoints[BoxVerts[x][0]].z);
		PrimBuild::end();
	} 
}

//------------------------------------------------------------------------------
bool fxFractalRenderList::IsQuadrantVisible(const Box3F VisBox, const MatrixF& RenderTransform)
{
	// Can we trivially accept the visible box?
	if (mBox.isOverlapped(VisBox))
	{
		// Yes, so calculate Object-Space Box.
		MatrixF InvXForm = RenderTransform;
		InvXForm.inverse();
		Box3F OSBox = VisBox;
		InvXForm.mulP(OSBox.min);
		InvXForm.mulP(OSBox.max);

		// Yes, so fetch Box Center.
		Point3F Center;
		OSBox.getCenter(&Center);

		// Scale.
		Point3F XRad(OSBox.len_x() * 0.5f, 0.0f, 0.0f);
		Point3F YRad(0.0f, OSBox.len_y() * 0.5f, 0.0f);
		Point3F ZRad(0.0f, 0.0f, OSBox.len_z() * 0.5f);

		// Render Transformation.
		RenderTransform.mulP(Center);
		RenderTransform.mulV(XRad);
		RenderTransform.mulV(YRad);
		RenderTransform.mulV(ZRad);

		// Check against View-planes.
		for (U32 i = 0; i < 5; i++)
		{
			// Reject if not visible.
			if (ViewPlanes[i].whichSideBox(Center, 
                                        XRad, YRad, ZRad, 
                                        Point3F(0, 0, 0)) == PlaneF::Back)
            return false;
		}

		// Visible.
		return true;
	}

	// Not visible.
	return false;
}



//------------------------------------------------------------------------------
//
// Class: fxFractalCulledList
//
//------------------------------------------------------------------------------
fxFractalCulledList::fxFractalCulledList(Box3F SearchBox, fxFractalCulledList* InVec)
{
	// Find the Candidates.
	FindCandidates(SearchBox, InVec);
}

//------------------------------------------------------------------------------

void fxFractalCulledList::FindCandidates(Box3F SearchBox, fxFractalCulledList* InVec)
{
	// Search the Culled List.
	for (U32 i = 0; i < InVec->GetListCount(); i++)
	{
		// Is this Box overlapping our search box?
		if (SearchBox.isOverlapped(InVec->GetElement(i)->FractalItemBox))
		{
			// Yes, so add it to our culled list.
			mCulledObjectSet.push_back(InVec->GetElement(i));
		}
	}
}



//------------------------------------------------------------------------------
//
// Class: fxFractalItemReplicator
//
//------------------------------------------------------------------------------

fxFractalItemReplicator::fxFractalItemReplicator()
{
	// Setup NetObject.
	mTypeMask |= StaticObjectType | StaticTSObjectType | StaticRenderedObjectType;
	mAddedToScene = false;
	mNetFlags.set(Ghostable | ScopeAlways);

	// Reset Client Replication Started.
	mClientReplicationStarted = false;

	// Reset Foliage Count.
	mCurrentFractalItemCount = 0;

	// Reset Creation Area Angle Animation.
	mCreationAreaAngle = 0;

	// Reset Last Render Time.
	mLastRenderTime = 0;

	// Reset Foliage Nodes.
	mPotentialFractalNodes = 0;
	
	// Reset Billboards Acquired.
	mBillboardsAcquired = 0;

	// Reset Frame Serial ID.
	mFrameSerialID = 0;

	mAlphaLookup = NULL;
}

//------------------------------------------------------------------------------

fxFractalItemReplicator::~fxFractalItemReplicator()
{
	if (mAlphaLookup)
		delete mAlphaLookup;
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::initPersistFields()
{
	// Initialise parents' persistent fields.
	Parent::initPersistFields();

	// Add out own persistent fields.
	addGroup( "Debugging" );	// MM: Added Group Header.
	addField( "UseDebugInfo",		TypeBool,		Offset( mFieldData.mUseDebugInfo,			fxFractalItemReplicator ) );
	addField( "DebugBoxHeight",		TypeF32,		Offset( mFieldData.mDebugBoxHeight,			fxFractalItemReplicator ) );
	addField( "HideFractal",		TypeBool,		Offset( mFieldData.mHideFractal,			fxFractalItemReplicator ) );
	endGroup( "Debugging" );	// MM: Added Group Footer.

	addGroup( "Media" );	// MM: Added Group Header.
	addField( "FractalItemFile",		TypeFilename,	Offset( mFieldData.mFractalItemFile,			fxFractalItemReplicator ) );
	addField( "FractalItemCount",		TypeS32,		Offset( mFieldData.mFractalItemCount,			fxFractalItemReplicator ) );
	endGroup( "Media" );	// MM: Added Group Footer.

	addGroup( "Pickover" );	// MM: Added Group Header.
	addField( "InputA",		TypeF32,		Offset( mFieldData.mInputA,			fxFractalItemReplicator ) );
	addField( "InputB",		TypeF32,		Offset( mFieldData.mInputB,			fxFractalItemReplicator ) );
	addField( "InputC",		TypeF32,		Offset( mFieldData.mInputC,			fxFractalItemReplicator ) );
	addField( "InputD",		TypeF32,		Offset( mFieldData.mInputD,			fxFractalItemReplicator ) );
	addField( "FractalScale",		TypeS32,		Offset( mFieldData.mFractalScale,			   fxFractalItemReplicator ) );
	endGroup( "Pickover" );	// MM: Added Group Footer.

	addGroup( "Dimensions" );	// MM: Added Group Header.
	addField( "MinWidth",			TypeF32,		Offset( mFieldData.mMinWidth,				fxFractalItemReplicator ) );
	addField( "MaxWidth",			TypeF32,		Offset( mFieldData.mMaxWidth,				fxFractalItemReplicator ) );
	addField( "MinHeight",			TypeF32,		Offset( mFieldData.mMinHeight,				fxFractalItemReplicator ) );
	addField( "MaxHeight",			TypeF32,		Offset( mFieldData.mMaxHeight,				fxFractalItemReplicator ) );
	addField( "FixAspectRatio",		TypeBool,		Offset( mFieldData.mFixAspectRatio,			fxFractalItemReplicator ) );
	addField( "FixSizeToMax",		TypeBool,		Offset( mFieldData.mFixSizeToMax,			fxFractalItemReplicator ) );
	addField( "RandomFlip",			TypeBool,		Offset( mFieldData.mRandomFlip,				fxFractalItemReplicator ) );
	endGroup( "Dimensions" );	// MM: Added Group Footer.

	addGroup( "Culling" );	// MM: Added Group Header.
	addField( "UseCulling",			TypeBool,		Offset( mFieldData.mUseCulling,				fxFractalItemReplicator ) );
	addField( "CullResolution",		TypeS32,		Offset( mFieldData.mCullResolution,			fxFractalItemReplicator ) );
	addField( "ViewDistance",		TypeF32,		Offset( mFieldData.mViewDistance,			fxFractalItemReplicator ) );
	addField( "ViewClosest",		TypeF32,		Offset( mFieldData.mViewClosest,			fxFractalItemReplicator ) );
	addField( "FadeInRegion",		TypeF32,		Offset( mFieldData.mFadeInRegion,			fxFractalItemReplicator ) );
	addField( "FadeOutRegion",		TypeF32,		Offset( mFieldData.mFadeOutRegion,			fxFractalItemReplicator ) );
	addField( "AlphaCutoff",		TypeF32,		Offset( mFieldData.mAlphaCutoff,			fxFractalItemReplicator ) );
	addField( "GroundAlpha",		TypeF32,		Offset( mFieldData.mGroundAlpha,			fxFractalItemReplicator ) );
	endGroup( "Culling" );	// MM: Added Group Footer.

	addGroup( "Lighting" );	// MM: Added Group Header.
	addField( "LightOn",			TypeBool,		Offset( mFieldData.mLightOn,				fxFractalItemReplicator ) );
	addField( "LightSync",			TypeBool,		Offset( mFieldData.mLightSync,				fxFractalItemReplicator ) );
	addField( "MinLuminance",		TypeF32,		Offset( mFieldData.mMinLuminance,			fxFractalItemReplicator ) );
	addField( "MaxLuminance",		TypeF32,		Offset( mFieldData.mMaxLuminance,			fxFractalItemReplicator ) );
	addField( "LightTime",			TypeF32,		Offset( mFieldData.mLightTime,				fxFractalItemReplicator ) );
	endGroup( "Lighting" );	// MM: Added Group Footer.
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::CreateFractal(void)
{
	F32				HypX, HypY;
	F32             posX, posY, posZ;
	Point3F			FractalItemPosition;

	// Let's get a minimum bounding volume.
	Point3F	MinPoint( -0.5, -0.5, -0.5 );
	Point3F	MaxPoint(  0.5,  0.5,  0.5 );

	// Check Host.
	AssertFatal(isClientObject(), "Trying to create Fractal on Server, this is bad!")

	// Cannot continue without Foliage Texture!
	if (dStrlen(mFieldData.mFractalItemFile) == 0) 
		return;

	// Destroy Foliage if we've already got some.
	if (mCurrentFractalItemCount != 0) DestroyFractal();

	// Inform the user if culling has been disabled!
	if (!mFieldData.mUseCulling)
	{
		// Console Output.
		Con::printf("fxFractalItemReplicator - Culling has been disabled!");
	}

	// ----------------------------------------------------------------------------------------------------------------------
	// Step 1.
	// ----------------------------------------------------------------------------------------------------------------------

	// Calculate the maximum dimension.
	//F32 MaxDimension = 2.0f * ( (mFieldData.mOuterRadiusX > mFieldData.mOuterRadiusY) ? mFieldData.mOuterRadiusX : mFieldData.mOuterRadiusY );
	F32 MaxDimension = 2.0f * 2000.0f;

	// Let's check that our cull resolution is not greater than half our maximum dimension (and less than 1).
	if (mFieldData.mCullResolution > (MaxDimension/2) || mFieldData.mCullResolution < 8)
	{
		// Problem ...
		Con::warnf(ConsoleLogEntry::General, "fxFractalItemReplicator - Could create FRactal, invalid Culling Resolution!");
		Con::warnf(ConsoleLogEntry::General, "fxFractalItemReplicator - Culling Resolution *must* be >=8 or <= %0.2f!", (MaxDimension/2));

		// Return here.
		return;
	}

	// Take first Timestamp.
	F32 mStartCreationTime = (F32) Platform::getRealMilliseconds();

	// Calculate the quad-tree levels needed for selected 'mCullResolution'.
	mQuadTreeLevels = (U32)(mCeil(mLog( MaxDimension / mFieldData.mCullResolution ) / mLog( 2.0f )));

	// Calculate the number of potential nodes required.
	mPotentialFractalNodes = 0;
	for (U32 n = 0; n <= mQuadTreeLevels; n++)
		mPotentialFractalNodes += (U32)(mCeil(mPow(4.0f, (F32) n)));	// Ceil to be safe!

	// ----------------------------------------------------------------------------------------------------------------------
	// Step 2.
	// ----------------------------------------------------------------------------------------------------------------------
    
    posX = 0;
    posY = 0;
    posZ = 0;
    
	// Add FractalItems.
	for (U32 idx = 0; idx < mFieldData.mFractalItemCount; idx++)
	{
		fxFractalItem*	pFractalItem;
		Point3F			FractalItemOffsetPos;
    
        // Get the fxFractalItemReplicator Position.
        FractalItemPosition = getPosition();

      // Pickover formula
	   HypX	= sin(mFieldData.mInputA * posY) - posZ * cos(mFieldData.mInputB * posX);
	   HypY	= posZ * sin(mFieldData.mInputC * posX) - cos(mFieldData.mInputD * posY);
	   posZ  = sin(posX);
	   posX = HypX;
	   posY = HypY;
		
	   // Calcualte the new position.
	   FractalItemPosition.x += posX * mFieldData.mFractalScale;
	   FractalItemPosition.y += posY * mFieldData.mFractalScale;
	   FractalItemPosition.z += posZ * mFieldData.mFractalScale;

		// Monitor the total volume.
		FractalItemOffsetPos = FractalItemPosition - getPosition();
		MinPoint.setMin(FractalItemOffsetPos);
		MaxPoint.setMax(FractalItemOffsetPos);

		// Create our Foliage Item.
		pFractalItem = new fxFractalItem;

		// Reset Frame Serial.
		pFractalItem->LastFrameSerialID = 0;

		// Reset Transform.
		pFractalItem->Transform.identity();

		// Set Position.
		pFractalItem->Transform.setColumn(3, FractalItemPosition);

		// Are we fixing size @ max?
		if (mFieldData.mFixSizeToMax)
		{
			// Yes, so set height maximum height.
			pFractalItem->Height = mFieldData.mMaxHeight;
			// Is the Aspect Ratio Fixed?
			if (mFieldData.mFixAspectRatio)
				// Yes, so lock to height.
				pFractalItem->Width = pFractalItem->Height;
			else
				// No, so set width to maximum width.
				pFractalItem->Width = mFieldData.mMaxWidth;
		}
		else
		{
			// No, so choose a new Scale.
			pFractalItem->Height = RandomGen.randF(mFieldData.mMinHeight, mFieldData.mMaxHeight);
			// Is the Aspect Ratio Fixed?
			if (mFieldData.mFixAspectRatio)
				// Yes, so lock to height.
				pFractalItem->Width = pFractalItem->Height;
			else
				// No, so choose a random width.
				pFractalItem->Width = RandomGen.randF(mFieldData.mMinWidth, mFieldData.mMaxWidth);
		}

		// Are we randomly flipping horizontally?
		if (mFieldData.mRandomFlip)
			// Yes, so choose a random flip for this object.
			pFractalItem->Flipped = (RandomGen.randF(0, 1000) < 500.0f) ? false : true;
		else
			// No, so turn-off flipping.
			pFractalItem->Flipped = false;		

		
	   // Give it a Minimum volume...
	   pFractalItem->FractalItemBox.min =	FractalItemPosition +
									   Point3F(-pFractalItem->Width / 2.0f,
											   -0.5f,
											   pFractalItem->Height );

	   pFractalItem->FractalItemBox.max =	FractalItemPosition +
									   Point3F(+pFractalItem->Width / 2.0f,
											   +0.5f,
											   pFractalItem->Height );

		// Store Shape in Replicated Shapes Vector.
		mReplicatedFractals.push_back(pFractalItem);

		// Increase Foliage Count.
		mCurrentFractalItemCount++;
	}

	// Is Lighting On?
	if (mFieldData.mLightOn)
	{
		// Yes, so reset Global Light phase.
		mGlobalLightPhase = 0.0f;
		// Set Global Light Time Ratio.
		mGlobalLightTimeRatio = PeriodLenMinus / mFieldData.mLightTime;
		
		// Yes, so step through Foliage.
		for (U32 idx = 0; idx < mCurrentFractalItemCount; idx++)
		{
			fxFractalItem*	pFractalItem;

			// Fetch the Foliage Item.
			pFractalItem = mReplicatedFractals[idx];

			// Do we have an item?
			if (pFractalItem)
			{
				// Yes, so are lights syncronised?
				if (mFieldData.mLightSync)
				{
					pFractalItem->LightTimeRatio = 1.0f;
					pFractalItem->LightPhase = 0.0f;
				}
				else
				{
					// No, so choose a random Light phase.
					pFractalItem->LightPhase = RandomGen.randF(0, PeriodLenMinus);
					// Set Light Time Ratio.
					pFractalItem->LightTimeRatio = PeriodLenMinus / mFieldData.mLightTime;
				}
			}
		}

	}

	// Update our Object Volume.
	mObjBox.min.set(MinPoint);
	mObjBox.max.set(MaxPoint);
	setTransform(mObjToWorld);

	// ----------------------------------------------------------------------------------------------------------------------
	// Step 3.
	// ----------------------------------------------------------------------------------------------------------------------

	// Reset Next Allocated Node to Stack base.
	mNextAllocatedNodeIdx = 0;

	// Allocate a new Node.
	fxFractalQuadrantNode* pNewNode = new fxFractalQuadrantNode;

	// Store it in the Quad-tree.
	mFractalQuadTree.push_back(pNewNode);

	// Populate Initial Node.
	//
	// Set Start Level.
	pNewNode->Level = mQuadTreeLevels;
	// Calculate Total Foliage Area.
	pNewNode->QuadrantBox = getWorldBox();
	// Reset Quadrant child nodes.
	pNewNode->QuadrantChildNode[0] =
	pNewNode->QuadrantChildNode[1] =
	pNewNode->QuadrantChildNode[2] =
	pNewNode->QuadrantChildNode[3] = NULL;

	// Create our initial cull list with *all* billboards into.
	fxFractalCulledList CullList;
	CullList.mCulledObjectSet = mReplicatedFractals;

	// Move to next node Index.
	mNextAllocatedNodeIdx++;

	// Let's start this thing going by recursing it's children.
	ProcessNodeChildren(pNewNode, &CullList);

	// Calculate Elapsed Time and take new Timestamp.
	F32 ElapsedTime = (Platform::getRealMilliseconds() - mStartCreationTime) * 0.001f;

	// Console Output.
	Con::printf("fxFractalItemReplicator - Lev: %d  PotNodes: %d  Used: %d  Objs: %d  Time: %0.4fs.",
				mQuadTreeLevels,
				mPotentialFractalNodes,
				mNextAllocatedNodeIdx-1,
				mBillboardsAcquired,
				ElapsedTime);

	// Dump (*very*) approximate allocated memory.
	F32 MemoryAllocated = (F32) ((mNextAllocatedNodeIdx-1) * sizeof(fxFractalQuadrantNode));
	MemoryAllocated		+=	mCurrentFractalItemCount * sizeof(fxFractalItem);
	MemoryAllocated		+=	mCurrentFractalItemCount * sizeof(fxFractalItem*);
	Con::printf("fxFractalItemReplicator - Approx. %0.2fMb allocated.", MemoryAllocated / 1048576.0f);

	// ----------------------------------------------------------------------------------------------------------------------

	SetupBuffers();

	// Take first Timestamp.
	mLastRenderTime = Platform::getVirtualMilliseconds();
}

// Ok, what we do is let the older code setup the FoliageItem list and the QuadTree.
// Then we build the Vertex and Primitive buffers here.  It would probably be
// slightly more memory efficient to build the buffers directly, but we
// want to sort the items within the buffer by the quadtreenodes
void fxFractalItemReplicator::SetupBuffers()
{
	// Following two arrays are used to build the vertex and primitive buffers.	
	Point3F basePoints[8];
	basePoints[0] = Point3F(-0.5f, 0.0f, 1.0f);
	basePoints[1] = Point3F(-0.5f, 0.0f, 0.0f);
	basePoints[2] = Point3F(0.5f, 0.0f, 0.0f);
	basePoints[3] = Point3F(0.5f, 0.0f, 1.0f);

	Point2F texCoords[4];
	texCoords[0] = Point2F(0.0, 0.0);
	texCoords[1] = Point2F(0.0, 1.0);
	texCoords[2] = Point2F(1.0, 1.0);
	texCoords[3] = Point2F(1.0, 0.0);	
	
	// Init our Primitive Buffer
	U32 indexSize = mFieldData.mFractalItemCount * 6;
	U16* indices = new U16[indexSize];
	// Two triangles per particle
	for (U16 i = 0; i < mFieldData.mFractalItemCount; i++) {
		U16* idx = &indices[i*6];		// hey, no offset math below, neat
		U16 vertOffset = i*4;
		idx[0] = vertOffset + 0;
		idx[1] = vertOffset + 1;
		idx[2] = vertOffset + 2;
		idx[3] = vertOffset + 2;
		idx[4] = vertOffset + 3;
		idx[5] = vertOffset + 0;
	}
	// Init the prim buffer and copy our indexes over
	U16 *ibIndices;
	mPrimBuffer.set(GFX, indexSize, 0, GFXBufferTypeStatic);
	mPrimBuffer.lock(&ibIndices);
	dMemcpy(ibIndices, indices, indexSize * sizeof(U16));
	mPrimBuffer.unlock();
	delete[] indices;

	// Now, let's init the vertex buffer
	U32 currPrimitiveStartIndex = 0;
	mVertexBuffer.set(GFX, mFieldData.mFractalItemCount * 4, GFXBufferTypeStatic);
	mVertexBuffer.lock();
	U32 idx = 0;	
	for (S32 qtIdx = 0; qtIdx < mFractalQuadTree.size(); qtIdx++) {
		fxFractalQuadrantNode* quadNode = mFractalQuadTree[qtIdx];
		if (quadNode->Level == 0) {
			quadNode->startIndex = currPrimitiveStartIndex;
			quadNode->primitiveCount = 0;
			// Ok, there should be data in here!
			for (S32 i = 0; i < quadNode->RenderList.size(); i++) {
				fxFractalItem* pFractalItem = quadNode->RenderList[i];
				if (pFractalItem->LastFrameSerialID == 0) {
					pFractalItem->LastFrameSerialID++;
					// Dump it into the vertex buffer
					for (U32 vertIndex = 0; vertIndex < 4; vertIndex++) {
						GFXVertexFractal *vert = &mVertexBuffer[(idx*4) + vertIndex];
						// This is the position of the billboard.
						vert->point = pFractalItem->Transform.getPosition();			
						// Normal contains the point of the billboard (except for the y component, see below)
						vert->normal = basePoints[vertIndex];

						vert->normal.x *= pFractalItem->Width;
						vert->normal.z *= pFractalItem->Height;
						// Handle texture coordinates
						vert->texCoord = texCoords[vertIndex];				
						if (pFractalItem->Flipped)
							vert->texCoord.x = 1.0f - vert->texCoord.x;

						vert->texCoord2.set(0.0f, 0.0f);

						// Handle lighting, lighting happens at the same time as global so this is just an offset.
						vert->normal.y = pFractalItem->LightPhase;
					}
					idx++;
					quadNode->primitiveCount += 2;
					currPrimitiveStartIndex += 6; 
				}
			}
		}
	}
	mVertexBuffer.unlock();	

	DestroyFractalItems();
}

//------------------------------------------------------------------------------

Box3F fxFractalItemReplicator::FetchQuadrant(Box3F Box, U32 Quadrant)
{
	Box3F QuadrantBox;

	// Select Quadrant.
	switch(Quadrant)
	{
		// UL.
		case 0:
			QuadrantBox.min = Box.min + Point3F(0, Box.len_y()/2, 0);
			QuadrantBox.max = QuadrantBox.min + Point3F(Box.len_x()/2, Box.len_y()/2, Box.len_z());
			break;

		// UR.
		case 1:
			QuadrantBox.min = Box.min + Point3F(Box.len_x()/2, Box.len_y()/2, 0);
			QuadrantBox.max = QuadrantBox.min + Point3F(Box.len_x()/2, Box.len_y()/2, Box.len_z());
			break;

		// LL.
		case 2:
			QuadrantBox.min = Box.min;
			QuadrantBox.max = QuadrantBox.min + Point3F(Box.len_x()/2, Box.len_y()/2, Box.len_z());
			break;

		// LR.
		case 3:
			QuadrantBox.min = Box.min + Point3F(Box.len_x()/2, 0, 0);
			QuadrantBox.max = QuadrantBox.min + Point3F(Box.len_x()/2, Box.len_y()/2, Box.len_z());
			break;

		default:
			return Box;
	}

	return QuadrantBox;
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::ProcessNodeChildren(fxFractalQuadrantNode* pParentNode, fxFractalCulledList* pCullList)
{
	// ---------------------------------------------------------------
	// Split Node into Quadrants and Process each.
	// ---------------------------------------------------------------

	// Process All Quadrants (UL/UR/LL/LR).
	for (U32 q = 0; q < 4; q++)
		ProcessQuadrant(pParentNode, pCullList, q);
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::ProcessQuadrant(fxFractalQuadrantNode* pParentNode, fxFractalCulledList* pCullList, U32 Quadrant)
{
	// Fetch Quadrant Box.
	const Box3F QuadrantBox = FetchQuadrant(pParentNode->QuadrantBox, Quadrant);

	// Create our new Cull List.
	fxFractalCulledList CullList(QuadrantBox, pCullList);

	// Did we get any objects?
	if (CullList.GetListCount() > 0)
	{
		// Yes, so allocate a new Node.
		fxFractalQuadrantNode* pNewNode = new fxFractalQuadrantNode;

		// Store it in the Quad-tree.
		mFractalQuadTree.push_back(pNewNode);

		// Move to next node Index.
		mNextAllocatedNodeIdx++;

		// Populate Quadrant Node.
		//
		// Next Sub-level.
		pNewNode->Level = pParentNode->Level - 1;
		// Calculate Quadrant Box.
		pNewNode->QuadrantBox = QuadrantBox;
		// Reset Child Nodes.
		pNewNode->QuadrantChildNode[0] =
		pNewNode->QuadrantChildNode[1] =
		pNewNode->QuadrantChildNode[2] =
		pNewNode->QuadrantChildNode[3] = NULL;

		// Put a reference in parent.
		pParentNode->QuadrantChildNode[Quadrant] = pNewNode;

		// If we're not at sub-level 0 then process this nodes children.
		if (pNewNode->Level != 0) ProcessNodeChildren(pNewNode, &CullList);
		// If we've reached sub-level 0 then store Cull List (for rendering).
		if (pNewNode->Level == 0)
		{
			// Store the render list from our culled object set.
			pNewNode->RenderList = CullList.mCulledObjectSet;
			// Keep track of the total billboard acquired.
			mBillboardsAcquired += CullList.GetListCount();
		}
	}
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::SyncFractalReplicators(void)
{
	// Check Host.
	AssertFatal(isServerObject(), "We *MUST* be on server when Synchronising Fractal Items!")

	// Find the Replicator Set.
	SimSet *fxFractalSet = dynamic_cast<SimSet*>(Sim::findObject("fxFractalSet"));

	// Return if Error.
	if (!fxFractalSet)
	{
		// Console Warning.
		Con::warnf("fxFractalItemReplicator - Cannot locate the 'fxFractalSet', this is bad!");
		// Return here.
		return;
	}

	// Parse Replication Object(s).
	for (SimSetIterator itr(fxFractalSet); *itr; ++itr)
	{
		// Fetch the Replicator Object.
		fxFractalItemReplicator* Replicator = static_cast<fxFractalItemReplicator*>(*itr);
		// Set Foliage Replication Mask.
		if (Replicator->isServerObject())
		{
			Con::printf("fxFractalItemReplicator - Restarting fxFractalItemReplicator Object...");
			Replicator->setMaskBits(FractalReplicationMask);
		}
	}

	// Info ...
	Con::printf("fxFractalItemReplicator - Client Foliage Sync has completed.");
}


//------------------------------------------------------------------------------
// Lets chill our memory requirements out a little
void fxFractalItemReplicator::DestroyFractalItems()
{
	// Remove shapes.
	for (S32 idx = 0; idx < mReplicatedFractals.size(); idx++)
	{
		fxFractalItem*	pFractalItem;

		// Fetch the Foliage Item.
		pFractalItem = mReplicatedFractals[idx];

		// Delete Shape.
		if (pFractalItem) delete pFractalItem;
	}
	// Clear the Replicated Foliage Vector.
	mReplicatedFractals.clear();

	// Clear out old references also
	for (S32 qtIdx = 0; qtIdx < mFractalQuadTree.size(); qtIdx++) {
		fxFractalQuadrantNode* quadNode = mFractalQuadTree[qtIdx];
		if (quadNode->Level == 0) {
			quadNode->RenderList.clear();
		}
	}
}

void fxFractalItemReplicator::DestroyFractal(void)
{
	// Check Host.
	AssertFatal(isClientObject(), "Trying to destroy Fractal on Server, this is bad!")

	// Destroy Quad-tree.
	mPotentialFractalNodes = 0;
	// Reset Billboards Acquired.
	mBillboardsAcquired = 0;

	// Finish if we didn't create any shapes.
	if (mCurrentFractalItemCount == 0) return;

	DestroyFractalItems();

	// Let's remove the Quad-Tree allocations.
	for (	Vector<fxFractalQuadrantNode*>::iterator QuadNodeItr = mFractalQuadTree.begin();
			QuadNodeItr != mFractalQuadTree.end();
			QuadNodeItr++ )
		{
			// Remove the node.
			delete *QuadNodeItr;
		}

	// Clear the Foliage Quad-Tree Vector.
	mFractalQuadTree.clear();

	// Clear the Frustum Render Set Vector.
	mFrustumRenderSet.mVisObjectSet.clear();

	// Reset Foliage Count.
	mCurrentFractalItemCount = 0;
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::StartUp(void)
{
	// Flag, Client Replication Started.
	mClientReplicationStarted = true;

	// Create foliage on Client.
	if (isClientObject()) CreateFractal();
}

//------------------------------------------------------------------------------

bool fxFractalItemReplicator::onAdd()
{
	if(!Parent::onAdd()) return(false);

	// Add the Replicator to the Replicator Set.
	dynamic_cast<SimSet*>(Sim::findObject("fxFractalSet"))->addObject(this);

	// Set Default Object Box.
	mObjBox.min.set( -0.5, -0.5, -0.5 );
	mObjBox.max.set(  0.5,  0.5,  0.5 );
	// Reset the World Box.
	resetWorldBox();
	// Set the Render Transform.
	setRenderTransform(mObjToWorld);

	// Add to Scene.
	addToScene();
	mAddedToScene = true;

	// Are we on the client?
    if ( isClientObject() )
	{
		// Yes, so load foliage texture.
		mFieldData.mFractalItemTexture = GFXTexHandle( mFieldData.mFractalItemFile, &GFXDefaultStaticDiffuseProfile );
		if ((GFXTextureObject*) mFieldData.mFractalItemTexture == NULL)
			Con::printf("fxFractalItemReplicator:  %s is an invalid or missing foliage texture file.", mFieldData.mFractalItemFile);
		mAlphaLookup = new GBitmap(AlphaTexLen, 1);
		computeAlphaTex();

		// If we are in the editor then we can manually startup replication.
		if (gEditingMission) 
			mClientReplicationStarted = true;

		// Let's init the shader too! This is managed by the GFX->Shader manager, so I don't
		// need to free it myself. 
		mShader = GFX->createShader("shaders/fxFoliageReplicatorV.hlsl", "shaders/fxFoliageReplicatorP.hlsl", (F32) 1.4f);
	}

	// Return OK.
	return(true);
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::onRemove()
{
	// Remove the Replicator from the Replicator Set.
	dynamic_cast<SimSet*>(Sim::findObject("fxFractalSet"))->removeObject(this);

	// Remove from Scene.
	removeFromScene();
	mAddedToScene = false;

	// Are we on the Client?
	if (isClientObject())
	{
		// Yes, so destroy Foliage.
		DestroyFractal();

		// Remove Texture.
		mFieldData.mFractalItemTexture = NULL;
	}

	// Do Parent.
	Parent::onRemove();
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::inspectPostApply()
{
	// Set Parent.
	Parent::inspectPostApply();

	// Set Foliage Replication Mask (this object only).
	setMaskBits(FractalReplicationMask);
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::onEditorEnable()
{
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::onEditorDisable()
{
}

//------------------------------------------------------------------------------

ConsoleFunction(StartFractalReplication, void, 1, 1, "StartFractalReplication()")
{
	argv; argc; 
	// Find the Replicator Set.
	SimSet *fxFractalSet = dynamic_cast<SimSet*>(Sim::findObject("fxFractalSet"));

	// Return if Error.
	if (!fxFractalSet)
	{
		// Console Warning.
		Con::warnf("fxFractalItemReplicator - Cannot locate the 'fxFractalSet', this is bad!");
		// Return here.
		return;
	}

	// Parse Replication Object(s).
   U32 startupCount = 0;
	for (SimSetIterator itr(fxFractalSet); *itr; ++itr)
	{
		// Fetch the Replicator Object.
		fxFractalItemReplicator* Replicator = static_cast<fxFractalItemReplicator*>(*itr);
		
      // Start Client Objects Only.
		if (Replicator->isClientObject())
      {
         Replicator->StartUp();
         startupCount++;
      }
	}

	// Info ...
	Con::printf("fxFractalItemReplicator - replicated client Fractal Items for %d objects", startupCount);
}

//------------------------------------------------------------------------------

bool fxFractalItemReplicator::prepRenderImage(SceneState* state, const U32 stateKey, const U32 /*startZone*/,
								const bool /*modifyBaseZoneState*/)
{
	// Return if last state.
	if (isLastState(state, stateKey)) return false;
	// Set Last State.
	setLastState(state, stateKey);

   // Is Object Rendered?
   if (state->isObjectRendered(this))
   {
      RenderInst *ri = gRenderInstManager.allocInst();
      ri->obj = this;
      ri->state = state;
      ri->type = RenderInstManager::RIT_Foliage;
      gRenderInstManager.addInst( ri );
   }

   return false;
}

//
// RENDERING
//
void fxFractalItemReplicator::computeAlphaTex()
{
	// Distances used in alpha
	const F32	ClippedViewDistance		= mFieldData.mViewDistance;
	const F32	MaximumViewDistance		= ClippedViewDistance + mFieldData.mFadeInRegion;

	// This is used for the alpha computation in the shader.
	for (U32 i = 0; i < AlphaTexLen; i++) {
		F32 Distance = ((float) i / (float) AlphaTexLen) * MaximumViewDistance;
		F32 ItemAlpha = 1.0f;
		// Are we fading out?
		if (Distance < mFieldData.mViewClosest)
		{
			// Yes, so set fade-out.
			ItemAlpha = 1.0f - ((mFieldData.mViewClosest - Distance) * mFadeOutGradient);
		}
		// No, so are we fading in?
		else if (Distance > ClippedViewDistance)
		{
			// Yes, so set fade-in
			ItemAlpha = 1.0f - ((Distance - ClippedViewDistance) * mFadeInGradient);
		}

		// Set texture info
		ColorI c((U8) (255.0f * ItemAlpha), 0, 0);
		mAlphaLookup->setColor(i, 0, c);
	}
	mAlphaTexture.set(mAlphaLookup, &GFXDefaultStaticDiffuseProfile, false);
}

void fxFractalItemReplicator::renderObject(SceneState* state, RenderInst *)
{
   // If we're rendering and we haven't placed any foliage yet - do it.
   if(!mClientReplicationStarted)
   {
      Con::warnf("fxFractalItemReplicator::renderObject - tried to render a non replicated fxFractalItemReplicator; replicating it now...");

      StartUp();
   }

	// Calculate Elapsed Time and take new Timestamp.
	S32 Time = Platform::getVirtualMilliseconds();
	F32 ElapsedTime = (Time - mLastRenderTime) * 0.001f;
	mLastRenderTime = Time;	

	//renderPlacementArea(ElapsedTime);

	if (mCurrentFractalItemCount > 0) {

		if (!mFieldData.mHideFractal) {
				
			// Animate Global Light Phase (Modulus).
			mGlobalLightPhase = mGlobalLightPhase + (mGlobalLightTimeRatio * ElapsedTime);

			// Compute other light parameters
			const F32	LuminanceMidPoint		= (mFieldData.mMinLuminance + mFieldData.mMaxLuminance) / 2.0f;
			const F32	LuminanceMagnitude		= mFieldData.mMaxLuminance - LuminanceMidPoint;

			// Distances used in alpha
			const F32	ClippedViewDistance		= mFieldData.mViewDistance;
			const F32	MaximumViewDistance		= ClippedViewDistance + mFieldData.mFadeInRegion;

			// Set up our shader constants	
			// Projection matrix
			MatrixF proj = GFX->getProjectionMatrix();
			proj.transpose();
			GFX->setVertexShaderConstF(0, (float*)&proj, 4);
			// World transform matrix
			MatrixF world = GFX->getWorldMatrix();
			world.transpose();
			GFX->setVertexShaderConstF(4, (float*)&world, 4);
			// Light params
			GFX->setVertexShaderConstF(11, (float*) &mGlobalLightPhase, 1);
			GFX->setVertexShaderConstF(12, (float*) &LuminanceMagnitude, 1);
			GFX->setVertexShaderConstF(13, (float*) &LuminanceMidPoint, 1);	
			// Alpha params
			Point3F camPos = state->getCameraPosition();
			GFX->setVertexShaderConstF(14, (float*) &MaximumViewDistance, 1);
			GFX->setVertexShaderConstF(15, (float*) &camPos, 1);
			// Pixel shader constants, this one is for ground alpha
			GFX->setPixelShaderConstF(1, (float*) &mFieldData.mGroundAlpha, 1);			

			// Blend ops
			GFX->setAlphaBlendEnable( true );
			GFX->setAlphaTestEnable( true );
			GFX->setSrcBlend(GFXBlendSrcAlpha);
			GFX->setDestBlend(GFXBlendInvSrcAlpha);
			GFX->setAlphaFunc(GFXCmpGreater);
			GFX->setAlphaRef((U8) (255.0f * mFieldData.mAlphaCutoff));
			GFX->setCullMode(GFXCullNone);

			// Set up our texture and color ops.
			mShader->process();
			GFX->setTexture(0, mFieldData.mFractalItemTexture);
			// computeAlphaTex();		// Uncomment if we figure out how to clamp to fogAndHaze
			GFX->setTexture(1, mAlphaTexture);
			GFX->setTextureStageColorOp(0, GFXTOPModulate);
			GFX->setTextureStageColorOp(1, GFXTOPModulate);  // am I needed? 
			GFX->setTextureStageAddressModeU(1, GFXAddressClamp);
			GFX->setTextureStageAddressModeV(1, GFXAddressClamp);

			// Setup our buffers
			GFX->setVertexBuffer(mVertexBuffer);
			GFX->setPrimitiveBuffer(mPrimBuffer);

			// If we use culling, we're going to send chunks of our buffers to the card
			if (mFieldData.mUseCulling)
			{
				// Setup the Clip-Planes.
				F32 FarClipPlane = getMin((F32)state->getFarPlane(), 
                           mFieldData.mViewDistance + mFieldData.mFadeInRegion);
				mFrustumRenderSet.SetupClipPlanes(state, FarClipPlane);

				renderQuad(mFractalQuadTree[0], getRenderTransform(), false);

				// Multipass, don't want to interrupt the vb state 
				if (mFieldData.mUseDebugInfo) 
            {
					// hey man, we're done, so it doesn't matter if we kill it to render the next part
					GFX->setTextureStageColorOp(0, GFXTOPDisable);
					GFX->setTextureStageColorOp(1, GFXTOPDisable);
					GFX->disableShaders(); 
					renderQuad(mFractalQuadTree[0], getRenderTransform(), true);
				}
			}
         else 
         {	
				// Draw the whole shebang!
				GFX->drawIndexedPrimitive(GFXTriangleList, 0, mVertexBuffer->mNumVerts, 
                                       0, mPrimBuffer->mIndexCount / 3);
			}

			// Reset some states 
			GFX->setAlphaBlendEnable( false );
			GFX->setAlphaTestEnable( false );
			GFX->setTextureStageColorOp(0, GFXTOPDisable);
			GFX->setTextureStageColorOp(1, GFXTOPDisable);  
			GFX->disableShaders();		// this fixes editor issue.
		}
	}
}

void fxFractalItemReplicator::renderQuad(fxFractalQuadrantNode* quadNode, const MatrixF& RenderTransform, const bool UseDebug)
{
	if (quadNode != NULL) {
		if (mFrustumRenderSet.IsQuadrantVisible(quadNode->QuadrantBox, RenderTransform))
		{
			// Draw the Quad Box (Debug Only).
			if (UseDebug) 
				mFrustumRenderSet.DrawQuadBox(quadNode->QuadrantBox, ColorF(0.0f, 1.0f, 0.1f, 1.0f));
			if (quadNode->Level != 0) {
				for (U32 i = 0; i < 4; i++)
					renderQuad(quadNode->QuadrantChildNode[i], RenderTransform, UseDebug);
			} else {
				if (!UseDebug)
               if(quadNode->primitiveCount)
					   GFX->drawIndexedPrimitive(GFXTriangleList, 0, mVertexBuffer->mNumVerts, 
						         quadNode->startIndex, quadNode->primitiveCount);
			}
		} else {
			// Use a different color to say "I think I'm not visible!"
			if (UseDebug) 
				mFrustumRenderSet.DrawQuadBox(quadNode->QuadrantBox, ColorF(1.0f, 0.8f, 0.1f, 1.0f));
		}
	}
}

//------------------------------------------------------------------------------
// NETWORK
//------------------------------------------------------------------------------

U32 fxFractalItemReplicator::packUpdate(NetConnection * con, U32 mask, BitStream * stream)
{
	// Pack Parent.
	U32 retMask = Parent::packUpdate(con, mask, stream);

	// Write Foliage Replication Flag.
	if (stream->writeFlag(mask & FractalReplicationMask))
	{
		stream->writeAffineTransform(mObjToWorld);						// Foliage Master-Object Position.

		stream->writeFlag(mFieldData.mUseDebugInfo);					// Foliage Debug Information Flag.
		stream->write(mFieldData.mDebugBoxHeight);						// Foliage Debug Height.
		
		stream->write(mFieldData.mInputA);								// Input A
		stream->write(mFieldData.mInputB);								// Input B
		stream->write(mFieldData.mInputC);								// Input C
		stream->write(mFieldData.mInputD);								// Input D
		stream->write(mFieldData.mFractalScale);								// mScale
		
		stream->write(mFieldData.mFractalItemCount);						// Foliage Count.
		stream->writeString(mFieldData.mFractalItemFile);					// Foliage File.

		stream->write(mFieldData.mMinWidth);							// Foliage Minimum Width.
		stream->write(mFieldData.mMaxWidth);							// Foliage Maximum Width.
		stream->write(mFieldData.mMinHeight);							// Foliage Minimum Height.
		stream->write(mFieldData.mMaxHeight);							// Foliage Maximum Height.
		stream->write(mFieldData.mFixAspectRatio);						// Foliage Fix Aspect Ratio.
		stream->write(mFieldData.mFixSizeToMax);						// Foliage Fix Size to Max.
		stream->write(mFieldData.mRandomFlip);							// Foliage Random Flip.

		stream->write(mFieldData.mUseCulling);							// Foliage Use Culling.
		stream->write(mFieldData.mCullResolution);						// Foliage Cull Resolution.
		stream->write(mFieldData.mViewDistance);						// Foliage View Distance.
		stream->write(mFieldData.mViewClosest);							// Foliage View Closest.
		stream->write(mFieldData.mFadeInRegion);						// Foliage Fade-In Region.
		stream->write(mFieldData.mFadeOutRegion);						// Foliage Fade-Out Region.
		stream->write(mFieldData.mAlphaCutoff);							// Foliage Alpha Cutoff.
		stream->write(mFieldData.mGroundAlpha);							// Foliage Ground Alpha.

		stream->writeFlag(mFieldData.mLightOn);							// Foliage Light On Flag.
		stream->writeFlag(mFieldData.mLightSync);						// Foliage Light Sync
		stream->write(mFieldData.mMinLuminance);						// Foliage Minimum Luminance.
		stream->write(mFieldData.mMaxLuminance);						// Foliage Maximum Luminance.
		stream->write(mFieldData.mLightTime);							// Foliage Light Time.

		stream->writeFlag(mFieldData.mHideFractal);						// Hide Foliage.
	}

	// Were done ...
	return(retMask);
}

//------------------------------------------------------------------------------

void fxFractalItemReplicator::unpackUpdate(NetConnection * con, BitStream * stream)
{
	// Unpack Parent.
	Parent::unpackUpdate(con, stream);

	// Read Replication Details.
	if(stream->readFlag())
	{
		MatrixF		ReplicatorObjectMatrix;

		stream->readAffineTransform(&ReplicatorObjectMatrix);			// Foliage Master Object Position.

		mFieldData.mUseDebugInfo = stream->readFlag();					// Foliage Debug Information Flag.
		stream->read(&mFieldData.mDebugBoxHeight);						// Foliage Debug Height.
		
		stream->read(&mFieldData.mInputA);
		stream->read(&mFieldData.mInputB);
		stream->read(&mFieldData.mInputC);
		stream->read(&mFieldData.mInputD);
		stream->read(&mFieldData.mFractalScale);	
		
		stream->read(&mFieldData.mFractalItemCount);						// Foliage Count.
		mFieldData.mFractalItemFile = stream->readSTString();				// Foliage File.

		stream->read(&mFieldData.mMinWidth);							// Foliage Minimum Width.
		stream->read(&mFieldData.mMaxWidth);							// Foliage Maximum Width.
		stream->read(&mFieldData.mMinHeight);							// Foliage Minimum Height.
		stream->read(&mFieldData.mMaxHeight);							// Foliage Maximum Height.
		stream->read(&mFieldData.mFixAspectRatio);						// Foliage Fix Aspect Ratio.
		stream->read(&mFieldData.mFixSizeToMax);						// Foliage Fix Size to Max.
		stream->read(&mFieldData.mRandomFlip);							// Foliage Random Flip.

		stream->read(&mFieldData.mUseCulling);							// Foliage Use Culling.
		stream->read(&mFieldData.mCullResolution);						// Foliage Cull Resolution.
		stream->read(&mFieldData.mViewDistance);						// Foliage View Distance.
		stream->read(&mFieldData.mViewClosest);							// Foliage View Closest.
		stream->read(&mFieldData.mFadeInRegion);						// Foliage Fade-In Region.
		stream->read(&mFieldData.mFadeOutRegion);						// Foliage Fade-Out Region.
		stream->read(&mFieldData.mAlphaCutoff);							// Foliage Alpha Cutoff.
		stream->read(&mFieldData.mGroundAlpha);							// Foliage Ground Alpha.

		mFieldData.mLightOn = stream->readFlag();						// Foliage Light On Flag.
		mFieldData.mLightSync = stream->readFlag();						// Foliage Light Sync
		stream->read(&mFieldData.mMinLuminance);						// Foliage Minimum Luminance.
		stream->read(&mFieldData.mMaxLuminance);						// Foliage Maximum Luminance.
		stream->read(&mFieldData.mLightTime);							// Foliage Light Time.

		mFieldData.mHideFractal = stream->readFlag();					// Hide Foliage.


		// Calculate Fade-In/Out Gradients.
		mFadeInGradient		= 1.0f / mFieldData.mFadeInRegion;
		mFadeOutGradient	= 1.0f / mFieldData.mFadeOutRegion;

		// Set Transform.
		setTransform(ReplicatorObjectMatrix);

		// Load Foliage Texture on the client.
		mFieldData.mFractalItemTexture = GFXTexHandle( mFieldData.mFractalItemFile, &GFXDefaultStaticDiffuseProfile );
		if ((GFXTextureObject*) mFieldData.mFractalItemTexture == NULL)
			Con::printf("fxFractalItemReplicator:  %s is an invalid or missing foliage texture file.", mFieldData.mFractalItemFile);

		// Set Quad-Tree Box Height Lerp.
		mFrustumRenderSet.mHeightLerp = mFieldData.mDebugBoxHeight;

		// Create Foliage (if Replication has begun).
		if (mClientReplicationStarted) CreateFractal();
	}
}

//#pragma warning( default : 4127 )
#pragma warning( pop ) 