#pragma once

struct Scene
{
	Scene();
	~Scene();
	void ListNodes(Camera& camera, vector<SceneNode*>& visible_nodes);

	Vec2 fog_range;
	Color clear_color, fog_color, ambient_color;
	bool use_fog;

private:
	vector<SceneNode*> nodes;
};
