#include "EnginePch.h"
#include "Core.h"
#include "Profiler.h"

struct Profiler::Entry : ObjectPoolProxy<Profiler::Entry>
{
	string name;
	int64 tick, end;
	float percent;
	int frames;
	vector<Entry*> e;

	void OnFree()
	{
		for(Entry* child : e)
			child->SafeFree();
		e.clear();
	}
	void UpdatePercent(int64 total);
	void Merge(Entry* e2);
};

Profiler Profiler::g_profiler;

Profiler::Profiler() : started(false), enabled(false), e(nullptr), prev_e(nullptr)
{
}

Profiler::~Profiler()
{
	if(e)
		e->SafeFree();
	if(prev_e)
		prev_e->SafeFree();
}

void Profiler::Start()
{
	if(started)
		End();

	timer.Start();
	e = Entry::Get();
	e->name = "Start";
	timer.GetTime(e->tick);
	stac.push_back(e);
	started = true;
	if(!enabled)
	{
		frames = 0;
		enabled = true;
	}
}

void Profiler::End()
{
	if(!started)
		return;

	// finish root entry
	++frames;
	timer.Tick();
	timer.GetTime(e->end);

	// update percentage
	int64 total = e->end - e->tick;
	e->UpdatePercent(total);

	// merge and print
	if(prev_e)
	{
		e->Merge(prev_e);
		prev_e->Free();
	}
	prev_e = e;
	e = nullptr;
	if(frames >= 30)
	{
		frames = 0;
		str.clear();
		Print(prev_e, 0);
		prev_e->Free();
		prev_e = nullptr;
	}

	stac.clear();
	started = false;
}

void Profiler::Push(cstring name)
{
	if(name && started)
	{
		Entry* new_e = Entry::Get();
		new_e->name = name;
		timer.Tick();
		timer.GetTime(new_e->tick);
		stac.back()->e.push_back(new_e);
		stac.push_back(new_e);
	}
}

void Profiler::Pop()
{
	if(started && stac.size() > 1)
	{
		timer.Tick();
		timer.GetTime(stac.back()->end);
		stac.pop_back();
	}
}

void Profiler::Clear()
{
	str.clear();
	enabled = false;
}

void Profiler::Print(Entry* e, int tab)
{
	for(int i = 0; i < tab; ++i)
		str += '-';
	str += Format("%s (%g%% - %lld)\n", e->name.c_str(), FLT10(e->percent / e->frames), e->end - e->tick);
	for(vector<Entry*>::iterator it = e->e.begin(), end = e->e.end(); it != end; ++it)
		Print(*it, tab + 1);
}

void Profiler::Entry::UpdatePercent(int64 total)
{
	percent = 100.f*(end - tick) / total;
	frames = 1;
	for(Entry* entry : e)
		entry->UpdatePercent(total);
}

void Profiler::Entry::Merge(Entry* e2)
{
	percent += e2->percent;
	frames = e2->frames + 1;
	for(vector<Entry*>::iterator it = e.begin(), end = e.end(); it != end; ++it)
	{
		Entry* found_e = nullptr;
		for(vector<Entry*>::iterator it2 = e2->e.begin(), end2 = e2->e.end(); it2 != end2; ++it2)
		{
			if((*it)->name == (*it2)->name)
			{
				found_e = *it2;
				break;
			}
		}
		if(found_e)
			(*it)->Merge(found_e);
		else
			(*it)->frames = 1;
	}
}

ProfilerBlock::ProfilerBlock(cstring name)
{
	assert(name);
	if(Profiler::g_profiler.IsStarted())
	{
		Profiler::g_profiler.Push(name);
		on = true;
	}
	else
		on = false;
}

ProfilerBlock::ProfilerBlock(delegate<cstring()> action)
{
	if(Profiler::g_profiler.IsStarted())
	{
		cstring name = action();
		Profiler::g_profiler.Push(name);
		on = true;
	}
	else
		on = false;
}

ProfilerBlock::~ProfilerBlock()
{
	if(on)
		Profiler::g_profiler.Pop();
}
