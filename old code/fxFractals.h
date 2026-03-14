//-----------------------------------------------------------------------------
// Torque Game Engine Advanced
// Written by Melvyn May, 4th August 2002.
//-----------------------------------------------------------------------------

#ifndef _FRACTALITEMREPLICATOR_H_
#define _FRACTALITEMREPLICATOR_H_

#ifndef _SCENEOBJECT_H_
#include "sceneGraph/sceneObject.h"
#endif

#ifndef _GFXTEXTUREHANDLE_H_
#include "gfx/gfxTextureHandle.h"
#endif

#pragma warning( push, 4 )

#define AREA_ANIMATION_ARC         (   1.0f / 360.0f)

#define FXFRACTAL_COLLISION_MASK   (   TerrainObjectType     |   \
                                       AtlasObjectType       |   \
                                       InteriorObjectType    |   \
                                       StaticObjectType      |   \
                                       WaterObjectType      )

#define FXFRACTAL_NOWATER_COLLISION_MASK   ( TerrainObjectType      |   \
                                             InteriorObjectType      |   \
                                             StaticObjectType   )


#define FXFRACTALITEM_ALPHA_EPSILON          1e-4



//------------------------------------------------------------------------------
// Class: fxFractalItem
//------------------------------------------------------------------------------
class fxFractalItem
{
public:
   MatrixF     Transform;		
   F32         Width;			
   F32         Height;			
   Box3F			FractalItemBox;		
   bool			Flipped;			
   F32         SwayPhase;     
   F32         SwayTimeRatio; 
   F32         LightPhase;		
   F32         LightTimeRatio; 
	U32         LastFrameSerialID; 
};

//------------------------------------------------------------------------------
// Class: fxFractalCulledList
//------------------------------------------------------------------------------
class fxFractalCulledList
{
public:
   fxFractalCulledList() {};
   fxFractalCulledList(Box3F SearchBox, fxFractalCulledList* InVec);
   ~fxFractalCulledList() {};

   void FindCandidates(Box3F SearchBox, fxFractalCulledList* InVec);

   U32 GetListCount(void) { return mCulledObjectSet.size(); };
   fxFractalItem* GetElement(U32 index) { return mCulledObjectSet[index]; };

   Vector<fxFractalItem*>   mCulledObjectSet;      // Culled Object Set.
};


//------------------------------------------------------------------------------
// Class: fxFractalQuadNode
//------------------------------------------------------------------------------
class fxFractalQuadrantNode {
public:
   U32                  Level;
   Box3F               QuadrantBox;
   fxFractalQuadrantNode*   QuadrantChildNode[4];
   Vector<fxFractalItem*>   RenderList;
	// Used in DrawIndexPrimitive call.
	U32							 startIndex;
	U32							 primitiveCount;
};


//------------------------------------------------------------------------------
// Class: fxFractalRenderList
//------------------------------------------------------------------------------
class fxFractalRenderList
{
public:
   Point3F               FarPosLeftUp;      // View Frustum.
   Point3F               FarPosLeftDown;
   Point3F               FarPosRightUp;
   Point3F               FarPosRightDown;
   Point3F               CameraPosition;      // Camera Position.
   Box3F               mBox;            // Clipping Box.
   PlaneF               ViewPlanes[5];      // Clipping View-Planes.

   Vector<fxFractalItem*>   mVisObjectSet;      // Visible Object Set.
   F32                  mHeightLerp;      // Height Lerp.

public:
   bool IsQuadrantVisible(const Box3F VisBox, const MatrixF& RenderTransform);
   void SetupClipPlanes(SceneState* state, const F32 FarClipPlane);
   void DrawQuadBox(const Box3F& QuadBox, const ColorF Colour);
};

#pragma warning(disable : 4100)

// Define a vertex 
DEFINE_VERT( GFXVertexFractal,
             GFXVertexFlagXYZ | GFXVertexFlagNormal | GFXVertexFlagTextureCount2 | 
             GFXVertexFlagUV0 | GFXVertexFlagUV1 )
{
   Point3F point;
   Point3F normal;
   Point2F texCoord;
   Point2F texCoord2;
};

#pragma warning(default : 4100)

//------------------------------------------------------------------------------
// Class: fxFractalItemReplicator
//------------------------------------------------------------------------------
class fxFractalItemReplicator : public SceneObject
{
private:
   typedef SceneObject      Parent;

protected:

   void CreateFractal(void);
   void DestroyFractal(void);
	void DestroyFractalItems();


   void SyncFractalReplicators(void);

   Box3F FetchQuadrant(Box3F Box, U32 Quadrant);
   void ProcessQuadrant(fxFractalQuadrantNode* pParentNode, fxFractalCulledList* pCullList, U32 Quadrant);
   void ProcessNodeChildren(fxFractalQuadrantNode* pParentNode, fxFractalCulledList* pCullList);

   enum {   FractalReplicationMask   = (1 << 0) };


   U32   mCreationAreaAngle;
   bool  mClientReplicationStarted;
   bool  mAddedToScene;
   U32   mCurrentFractalItemCount;

   Vector<fxFractalQuadrantNode*>   mFractalQuadTree;
   Vector<fxFractalItem*>           mReplicatedFractals;
   fxFractalRenderList              mFrustumRenderSet;

	GFXVertexBufferHandle<GFXVertexFractal> mVertexBuffer;
	GFXPrimitiveBufferHandle	mPrimBuffer;
	GFXShader*						mShader;
	GBitmap*							mAlphaLookup;

   MRandomLCG                 RandomGen;
   F32                        mFadeInGradient;
   F32                        mFadeOutGradient;
   S32                        mLastRenderTime;
   F32                        mGlobalLightPhase;
   F32                        mGlobalLightTimeRatio;
   U32                        mFrameSerialID;

   U32                        mQuadTreeLevels;            // Quad-Tree Levels.
   U32                        mPotentialFractalNodes;     // Potential Foliage Nodes.
   U32                        mNextAllocatedNodeIdx;      // Next Allocated Node Index.
   U32                        mBillboardsAcquired;        // Billboards Acquired.

	// Used for alpha lookup in the pixel shader
	GFXTexHandle					mAlphaTexture;
	
	void SetupBuffers(); 
	void renderBuffers(SceneState* state);
	//void renderPlacementArea(const F32 ElapsedTime);
	void renderQuad(fxFractalQuadrantNode* quadNode, const MatrixF& RenderTransform, const bool UseDebug);
	void computeAlphaTex();
public:
   fxFractalItemReplicator();
   ~fxFractalItemReplicator();

   void StartUp(void);
   void ShowReplication(void);
   void HideReplication(void);

   // SceneObject
   void renderObject(SceneState *state, RenderInst *ri);
   virtual bool prepRenderImage(SceneState *state, const U32 stateKey, const U32 startZone,
                        const bool modifyBaseZoneState = false);

   // SimObject
   bool onAdd();
   void onRemove();
   void onEditorEnable();
   void onEditorDisable();
   void inspectPostApply();

   // NetObject
   U32 packUpdate(NetConnection *conn, U32 mask, BitStream *stream);
   void unpackUpdate(NetConnection *conn, BitStream *stream);

   // ConObject.
   static void initPersistFields();

   // Field Data.
   class tagFieldData
   {
      public:

      bool              mUseDebugInfo;
      F32               mDebugBoxHeight;
      U32               mSeed;
      StringTableEntry  mFractalItemFile;
      GFXTexHandle      mFractalItemTexture;
      U32               mFractalItemCount;
      
      F32               mInputA;
      F32               mInputB;
      F32               mInputC;
      F32               mInputD;
      S32               mFractalScale;

      F32               mMinWidth;
      F32               mMaxWidth;
      F32               mMinHeight;
      F32               mMaxHeight;
      bool              mFixAspectRatio;
      bool              mFixSizeToMax;
      bool              mRandomFlip;

      bool              mUseCulling;
      U32               mCullResolution;
      F32               mViewDistance;
      F32               mViewClosest;
      F32               mFadeInRegion;
      F32               mFadeOutRegion;
      F32               mAlphaCutoff;
      F32               mGroundAlpha;

      bool              mLightOn;
      bool              mLightSync;
      F32               mMinLuminance;
      F32               mMaxLuminance;
      F32               mLightTime;

      bool            mHideFractal;

      tagFieldData()
      {
         // Set Defaults.
         mUseDebugInfo         = false;
         mDebugBoxHeight       = 1.0f;
         mFractalItemFile          = StringTable->insert("");
         mFractalItemTexture       = GFXTexHandle();
         mFractalItemCount         = 10000;
         
         mInputA = 2.0f;
         mInputB = 0.5f;
         mInputC = -0.6f;
         mInputD = -2.5f;
         mFractalScale = 2;
         
         mMinWidth             = 1;
         mMaxWidth             = 3;
         mMinHeight            = 1;
         mMaxHeight            = 5;
         mFixAspectRatio       = true;
         mFixSizeToMax         = false;
         mRandomFlip           = true;

         mUseCulling           = true;
         mCullResolution       = 64;
         mViewDistance         = 50.0f;
         mViewClosest          = 1.0f;
         mFadeInRegion         = 10.0f;
         mFadeOutRegion        = 1.0f;
         mAlphaCutoff          = 0.2f;
         mGroundAlpha          = 1.0f;

         mLightOn              = false;
         mLightSync            = false;
         mMinLuminance         = 0.7f;
         mMaxLuminance         = 1.0f;
         mLightTime            = 5.0f;

         mHideFractal          = false;
      }

   } mFieldData;

   // Declare Console Object.
   DECLARE_CONOBJECT(fxFractalItemReplicator);
};
#pragma warning( pop ) 
#endif // _FOLIAGEREPLICATOR_H_
