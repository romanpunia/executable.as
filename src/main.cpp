#include "runtime.h"
#include "program.h"

int main(int argc, char* argv[])
{
	int ExitCode = 0;
	Schedule* Queue;
	VirtualMachine* VM;
	Compiler* Unit;
	ProgramContext Contextual(argc, argv);
	Contextual.Path = argc > 0 ? argv[0] : "";
	Contextual.Module = OS::Path::GetFilename(Contextual.Path.c_str());
	Contextual.Program = GetProgramByteCode();
	ProgramEntrypoint Entrypoint;
	ProgramConfig Config;
	Config.Symbols =
	{
		{{CONFIG_SYMBOLS}}
	};
	Config.Submodules
	{
		{{CONFIG_SUBMODULES}}
	};
	Config.Addons
	{
		{{CONFIG_ADDONS}}
	};
	Config.Libraries
	{
		{{CONFIG_LIBRARIES}}
	};
	Config.Settings =
	{
		{{CONFIG_SETTINGS}}
	};
	Config.Modules = {{CONFIG_MODULES}};
	Config.CLibraries = {{CONFIG_CLIBRARIES}};
	Config.CSymbols = {{CONFIG_CSYMBOLS}};
	Config.Files = {{CONFIG_CFILES}};
	Config.JSON = {{CONFIG_JSON}};
	Config.Remotes = {{CONFIG_REMOTES}};
	Config.Debug = {{CONFIG_DEBUG}};
	Config.Translator = {{CONFIG_TRANSLATOR}};
	Config.Interactive = {{CONFIG_INTERACTIVE}};
	Config.EssentialsOnly = {{CONFIG_ESSENTIALS_ONLY}};
	Config.LoadByteCode = {{CONFIG_LOAD_BYTE_CODE}};
	Config.SaveByteCode = {{CONFIG_SAVE_BYTE_CODE}};
	Config.SaveSourceCode = {{CONFIG_SAVE_SOURCE_CODE}};

	Mavi::Initialize(Config.EssentialsOnly ? (size_t)Mavi::Preset::App : (size_t)Mavi::Preset::Game);
	{
		VM = new VirtualMachine();
		Unit = VM->CreateCompiler();

		Queue = Schedule::Get();
		Queue->SetImmediate(true);
		Multiplexer::Create();

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

		TypeInfo Type = VM->GetTypeInfoByDecl("array<string>@");
		Bindings::Array* ArgsArray = Bindings::Array::Compose<String>(Type.GetTypeInfo(), Contextual.Args);
		Context->Execute(Main, [&Main, ArgsArray](ImmediateContext* Context)
		{
			if (Main.GetArgsCount() > 0)
				Context->SetArgObject(0, ArgsArray);
		}).Wait();

		int ExitCode = Main.GetReturnTypeId() == (int)TypeId::VOIDF ? 0 : (int)Context->GetReturnDWord();
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