#include "EnginePch.h"
#include "EngineCore.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Camera.h"

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
