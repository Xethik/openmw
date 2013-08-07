#include "objects.hpp"

#include <cmath>

#include <OgreSceneNode.h>
#include <OgreSceneManager.h>
#include <OgreEntity.h>
#include <OgreLight.h>
#include <OgreSubEntity.h>
#include <OgreParticleSystem.h>
#include <OgreParticleEmitter.h>
#include <OgreStaticGeometry.h>

#include <components/nifogre/ogrenifloader.hpp>
#include <components/settings/settings.hpp>

#include "../mwworld/ptr.hpp"
#include "../mwworld/class.hpp"

#include "renderconst.hpp"
#include "animation.hpp"

using namespace MWRender;

int Objects::uniqueID = 0;

void Objects::setRootNode(Ogre::SceneNode* root)
{
    mRootNode = root;
}

void Objects::insertBegin (const MWWorld::Ptr& ptr)
{
    Ogre::SceneNode* root = mRootNode;
    Ogre::SceneNode* cellnode;
    if(mCellSceneNodes.find(ptr.getCell()) == mCellSceneNodes.end())
    {
        //Create the scenenode and put it in the map
        cellnode = root->createChildSceneNode();
        mCellSceneNodes[ptr.getCell()] = cellnode;
    }
    else
    {
        cellnode = mCellSceneNodes[ptr.getCell()];
    }

    Ogre::SceneNode* insert = cellnode->createChildSceneNode();
    const float *f = ptr.getRefData().getPosition().pos;

    insert->setPosition(f[0], f[1], f[2]);
    insert->setScale(ptr.getCellRef().mScale, ptr.getCellRef().mScale, ptr.getCellRef().mScale);


    // Convert MW rotation to a quaternion:
    f = ptr.getCellRef().mPos.rot;

    // Rotate around X axis
    Ogre::Quaternion xr(Ogre::Radian(-f[0]), Ogre::Vector3::UNIT_X);

    // Rotate around Y axis
    Ogre::Quaternion yr(Ogre::Radian(-f[1]), Ogre::Vector3::UNIT_Y);

    // Rotate around Z axis
    Ogre::Quaternion zr(Ogre::Radian(-f[2]), Ogre::Vector3::UNIT_Z);

    // Rotates first around z, then y, then x
    insert->setOrientation(xr*yr*zr);

    ptr.getRefData().setBaseNode(insert);
}

void Objects::insertMesh (const MWWorld::Ptr& ptr, const std::string& mesh)
{
    Ogre::SceneNode* insert = ptr.getRefData().getBaseNode();
    assert(insert);

    std::auto_ptr<ObjectAnimation> anim(new ObjectAnimation(ptr, mesh));

    Ogre::AxisAlignedBox bounds = anim->getWorldBounds();
    Ogre::Vector3 extents = bounds.getSize();
    extents *= insert->getScale();
    float size = std::max(std::max(extents.x, extents.y), extents.z);

    bool small = (size < Settings::Manager::getInt("small object size", "Viewing distance")) &&
                 Settings::Manager::getBool("limit small object distance", "Viewing distance");
    // do not fade out doors. that will cause holes and look stupid
    if(ptr.getTypeName().find("Door") != std::string::npos)
        small = false;

    if (mBounds.find(ptr.getCell()) == mBounds.end())
        mBounds[ptr.getCell()] = Ogre::AxisAlignedBox::BOX_NULL;
    mBounds[ptr.getCell()].merge(bounds);

    if(ptr.getTypeName() == typeid(ESM::Light).name())
        anim->addLight(ptr.get<ESM::Light>()->mBase);

    if(ptr.getTypeName() == typeid(ESM::Static).name() &&
       Settings::Manager::getBool("use static geometry", "Objects") &&
       anim->canBatch())
    {
        Ogre::StaticGeometry* sg = 0;

        if (small)
        {
            if(mStaticGeometrySmall.find(ptr.getCell()) == mStaticGeometrySmall.end())
            {
                uniqueID = uniqueID+1;
                sg = mRenderer.getScene()->createStaticGeometry("sg" + Ogre::StringConverter::toString(uniqueID));
                mStaticGeometrySmall[ptr.getCell()] = sg;

                sg->setRenderingDistance(Settings::Manager::getInt("small object distance", "Viewing distance"));
            }
            else
                sg = mStaticGeometrySmall[ptr.getCell()];
        }
        else
        {
            if(mStaticGeometry.find(ptr.getCell()) == mStaticGeometry.end())
            {
                uniqueID = uniqueID+1;
                sg = mRenderer.getScene()->createStaticGeometry("sg" + Ogre::StringConverter::toString(uniqueID));
                mStaticGeometry[ptr.getCell()] = sg;
            }
            else
                sg = mStaticGeometry[ptr.getCell()];
        }

        // This specifies the size of a single batch region.
        // If it is set too high:
        //  - there will be problems choosing the correct lights
        //  - the culling will be more inefficient
        // If it is set too low:
        //  - there will be too many batches.
        sg->setRegionDimensions(Ogre::Vector3(2500,2500,2500));

        sg->setVisibilityFlags(small ? RV_StaticsSmall : RV_Statics);

        sg->setCastShadows(true);

        sg->setRenderQueueGroup(RQG_Main);

        anim->fillBatch(sg);
        /* TODO: We could hold on to this and just detach it from the scene graph, so if the Ptr
         * ever needs to modify we can reattach it and rebuild the StaticGeometry object without
         * it. Would require associating the Ptr with the StaticGeometry. */
        anim.reset();
    }

    if(anim.get() != NULL)
        mObjects.insert(std::make_pair(ptr, anim.release()));
}

bool Objects::deleteObject (const MWWorld::Ptr& ptr)
{
    if(!ptr.getRefData().getBaseNode())
        return true;

    PtrAnimationMap::iterator iter = mObjects.find(ptr);
    if(iter != mObjects.end())
    {
        delete iter->second;
        mObjects.erase(iter);

        mRenderer.getScene()->destroySceneNode(ptr.getRefData().getBaseNode());
        ptr.getRefData().setBaseNode(0);
        return true;
    }

    return false;
}


void Objects::removeCell(MWWorld::Ptr::CellStore* store)
{
    for(PtrAnimationMap::iterator iter = mObjects.begin();iter != mObjects.end();)
    {
        if(iter->first.getCell() == store)
        {
            delete iter->second;
            mObjects.erase(iter++);
        }
        else
            ++iter;
    }

    std::map<MWWorld::CellStore*,Ogre::StaticGeometry*>::iterator geom = mStaticGeometry.find(store);
    if(geom != mStaticGeometry.end())
    {
        Ogre::StaticGeometry *sg = geom->second;
        mStaticGeometry.erase(geom);
        mRenderer.getScene()->destroyStaticGeometry(sg);
    }

    geom = mStaticGeometrySmall.find(store);
    if(geom != mStaticGeometrySmall.end())
    {
        Ogre::StaticGeometry *sg = geom->second;
        mStaticGeometrySmall.erase(store);
        mRenderer.getScene()->destroyStaticGeometry(sg);
    }

    mBounds.erase(store);

    std::map<MWWorld::CellStore*,Ogre::SceneNode*>::iterator cell = mCellSceneNodes.find(store);
    if(cell != mCellSceneNodes.end())
    {
        cell->second->removeAndDestroyAllChildren();
        mRenderer.getScene()->destroySceneNode(cell->second);
        mCellSceneNodes.erase(cell);
    }
}

void Objects::buildStaticGeometry(MWWorld::Ptr::CellStore& cell)
{
    if(mStaticGeometry.find(&cell) != mStaticGeometry.end())
    {
        Ogre::StaticGeometry* sg = mStaticGeometry[&cell];
        sg->build();
    }
    if(mStaticGeometrySmall.find(&cell) != mStaticGeometrySmall.end())
    {
        Ogre::StaticGeometry* sg = mStaticGeometrySmall[&cell];
        sg->build();
    }
}

Ogre::AxisAlignedBox Objects::getDimensions(MWWorld::Ptr::CellStore* cell)
{
    return mBounds[cell];
}

void Objects::enableLights()
{
    PtrAnimationMap::const_iterator it = mObjects.begin();
    for(;it != mObjects.end();it++)
        it->second->enableLights(true);
}

void Objects::disableLights()
{
    PtrAnimationMap::const_iterator it = mObjects.begin();
    for(;it != mObjects.end();it++)
        it->second->enableLights(false);
}

void Objects::update(const float dt)
{
    PtrAnimationMap::const_iterator it = mObjects.begin();
    for(;it != mObjects.end();it++)
        it->second->runAnimation(dt);
}

void Objects::rebuildStaticGeometry()
{
    for (std::map<MWWorld::CellStore *, Ogre::StaticGeometry*>::iterator it = mStaticGeometry.begin(); it != mStaticGeometry.end(); ++it)
    {
        it->second->destroy();
        it->second->build();
    }

    for (std::map<MWWorld::CellStore *, Ogre::StaticGeometry*>::iterator it = mStaticGeometrySmall.begin(); it != mStaticGeometrySmall.end(); ++it)
    {
        it->second->destroy();
        it->second->build();
    }
}

void Objects::updateObjectCell(const MWWorld::Ptr &old, const MWWorld::Ptr &cur)
{
    Ogre::SceneNode *node;
    MWWorld::CellStore *newCell = cur.getCell();

    if(mCellSceneNodes.find(newCell) == mCellSceneNodes.end()) {
        node = mRootNode->createChildSceneNode();
        mCellSceneNodes[newCell] = node;
    } else {
        node = mCellSceneNodes[newCell];
    }
    node->addChild(cur.getRefData().getBaseNode());
}

