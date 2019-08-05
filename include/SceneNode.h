#pragma once

struct SceneNode : public ObjectPoolProxy<SceneNode>
{
	Vec3 pos;
	float radius;
	//vector<SceneNode*> childs;

	//void OnFree()
	//{
	//	Free(childs);
	//}
};
