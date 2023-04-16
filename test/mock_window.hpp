#include <window.hpp>
#include <iwindow_callbacks.hpp>


class MockWindow : public App::Window
{
public:
	virtual void open(int x0, int y0) override
	{
	}

	virtual void setup_callbacks(IWindowCallbacks& callbacks) override
	{
	}

	virtual void poll_events() override
	{
	}

	virtual bool should_close() override
	{
		return false;
	}
};