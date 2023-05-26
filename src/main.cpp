#include "../deps/mavi.as/src/runtime.h"
#include "program.hpp"
#include <signal.h>

Function Terminate = nullptr;

void stop(int sigv)
{
	if (sigv != SIGINT && sigv != SIGTERM)
        return;
    {
        if (Terminate.IsValid() && Unit->GetContext()->Execute(Terminate, nullptr).Get() == 0)
        {
            Terminate = nullptr;
            goto GracefulShutdown;
        }

        auto* App = Application::Get();
        if (App != nullptr && App->GetState() == ApplicationState::Active)
        {
            App->Stop();
            goto GracefulShutdown;
        }

        auto* Queue = Schedule::Get();
        if (Queue->IsActive())
        {
            Queue->Stop();
            goto GracefulShutdown;
        }

        return std::exit(JUMP_CODE + EXIT_KILL);
    }
GracefulShutdown:
    signal(sigv, &stop);
}
int main(int argc, char* argv[])
{
	int ExitCode = 0;
	Schedule* Queue;
	VirtualMachine* VM;
	Compiler* Unit;
	ProgramContext Contextual(argc, argv);
	Contextual.Path = OS::Directory::GetModule();
	Contextual.Module = argc > 0 ? argv[0] : "runtime";
#ifdef HAS_PROGRAM_HEX
    program_hex::foreach(&Contextual, [](void* Context, const char* Buffer, unsigned Size)
    {
        ProgramContext* Contextual = (ProgramContext*)Context;
	    Contextual->Program = Codec::HexDecode(Buffer, (size_t)Size);
    });
#else
    return -1;
#endif
    Vector<std::pair<uint32_t, size_t>> Settings = { {{BUILDER_CONFIG_SETTINGS}}};
	ProgramEntrypoint Entrypoint;
	ProgramConfig Config;
	Config.Libraries = { {{BUILDER_CONFIG_LIBRARIES}} };
	Config.Functions = { {{BUILDER_CONFIG_FUNCTIONS}} };
	Config.SystemAddons = { {{BUILDER_CONFIG_ADDONS}} };
	Config.CLibraries = {{BUILDER_CONFIG_CLIBRARIES}};
	Config.CFunctions = {{BUILDER_CONFIG_CFUNCTIONS}};
	Config.Addons = {{BUILDER_CONFIG_SYSTEM_ADDONS}};
	Config.Files = {{BUILDER_CONFIG_FILES}};
	Config.Remotes = {{BUILDER_CONFIG_REMOTES}};
	Config.Translator = {{BUILDER_CONFIG_TRANSLATOR}};
	Config.EssentialsOnly = {{BUILDER_CONFIG_ESSENTIALS_ONLY}};
    OS::Directory::SetWorking(Contextual.Path.c_str());
    signal(SIGINT, &stop);
    signal(SIGTERM, &stop);
#ifdef VI_UNIX
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
#endif
	Mavi::Initialize(Config.EssentialsOnly ? (size_t)Mavi::Preset::App : (size_t)Mavi::Preset::Game);
	{
		VM = new VirtualMachine();
		Unit = VM->CreateCompiler();

		Queue = Schedule::Get();
		Queue->SetImmediate(true);
		Multiplexer::Create();

        for (auto& Item : Settings)
            VM->SetProperty((Features)Item.first, Item.second);

		ExitCode = ConfigureEngine(Config, Contextual, VM);
		if (ExitCode != 0)
			goto FinishProgram;

		Unit = VM->CreateCompiler();
		if (Unit->Prepare(Contextual.Module) < 0)
		{
			VI_ERR("cannot prepare <%s> module scope", Contextual.Module);
			return JUMP_CODE + EXIT_PREPARE_FAILURE;
		}

		ByteCodeInfo Info;
		Info.Data.insert(Info.Data.begin(), Contextual.Program.begin(), Contextual.Program.end());
		if (Unit->LoadByteCode(&Info).Get() < 0)
		{
			VI_ERR("cannot load <%s> module bytecode", Contextual.Module);
			return JUMP_CODE + EXIT_LOADING_FAILURE;
		}

		Function Main = GetEntrypoint(Contextual, Entrypoint, Unit);
		if (!Main.IsValid())
			return JUMP_CODE + EXIT_ENTRYPOINT_FAILURE;

		ImmediateContext* Context = Unit->GetContext();
		Context->SetExceptionCallback([](ImmediateContext* Context)
		{
			if (!Context->WillExceptionBeCaught())
				std::exit(JUMP_CODE + EXIT_RUNTIME_FAILURE);
		});
		Terminate = Unit->GetModule().GetFunctionByDecl(Entrypoint.Terminate);

		TypeInfo Type = VM->GetTypeInfoByDecl("array<string>@");
		Bindings::Array* ArgsArray = Type.IsValid() ? Bindings::Array::Compose<String>(Type.GetTypeInfo(), Contextual.Args) : nullptr;
		Context->Execute(Main, [&Main, ArgsArray](ImmediateContext* Context)
		{
			if (Main.GetArgsCount() > 0)
				Context->SetArgObject(0, ArgsArray);
		}).Wait();

		int ExitCode = Main.GetReturnTypeId() == (int)TypeId::VOIDF ? 0 : (int)Context->GetReturnDWord();
        if (ArgsArray != nullptr)
    		VM->ReleaseObject(ArgsArray, Type);
	
    	AwaitContext(Queue, VM, Context);
		return ExitCode;
	}
FinishProgram:
	VI_RELEASE(Unit);
	VI_RELEASE(VM);
	Mavi::Uninitialize();
	return ExitCode;
}