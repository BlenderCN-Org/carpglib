#pragma once

class SceneManager
{
public:
	SceneManager();
	~SceneManager();
	void Draw();
	Scene* GetActiveScene() { return active_scene; }
	Camera* GetCamera() { return camera; }

private:
	Camera* camera;
	Scene* active_scene;
	vector<Scene*> scenes;
	vector<SceneNode*> visible_nodes;
};
