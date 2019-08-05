#pragma once

struct Scene
{
	void ListNodes(Camera& camera, vector<SceneNode*>& visible_nodes);

private:
	vector<SceneNode*> nodes;
};
