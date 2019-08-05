#pragma once

class SceneManager
{
public:
	SceneManager();
	~SceneManager();
	void Init();
	void Draw();
	Scene* GetActiveScene() { return active_scene; }
	Camera* GetCamera() { return camera; }

	bool use_fog;

private:
	SuperShader* shader;
	Camera* camera;
	Scene* active_scene;
	vector<Scene*> scenes;
	vector<SceneNode*> visible_nodes;
};
