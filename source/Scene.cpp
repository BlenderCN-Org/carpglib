#include "EnginePch.h"
#include "EngineCore.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Camera.h"

Scene::Scene() : clear_color(Color::Black), use_fog(true), fog_range(20.f, 40.f), fog_color(128, 128, 128), ambient_color(255, 255, 255)
{
}

Scene::~Scene()
{
	SceneNode::Free(nodes);
}

void Scene::ListNodes(Camera& camera, vector<SceneNode*>& visible_nodes)
{
	FrustumPlanes frustum;
	frustum.Set(camera.matViewProj);

	for(SceneNode* node : nodes)
	{
		if(frustum.SphereToFrustum(node->pos, node->radius))
			visible_nodes.push_back(node);
	}
}
