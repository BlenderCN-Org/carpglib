#pragma once

struct Scene
{
	Scene();
	~Scene();
	void ListNodes(Camera& camera, vector<SceneNode*>& visible_nodes);

	Vec3 light_dir;
	Vec2 fog_range;
	Color clear_color, fog_color, ambient_color, light_color;
	bool use_fog, use_light_dir;

private:
	vector<SceneNode*> nodes;
};
