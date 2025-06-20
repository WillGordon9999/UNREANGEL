# About

UNREANGEL is a version of Hazelight's Angelscript Integration plugin for Unreal Engine that has been
modified to be compatible with vanilla versions of Unreal Engine, not dependent on engine source modifications.

Please Note this plugin is early in development, several features are planned and more thorough testing
needs to be conducted.  Any feedback or testing is greatly appreciated.

Currently Windows is the only tested platform with Unreal Engine 5.4.x

----------

# Features

- Create gameplay code and classes in Angelscript
- Recompile and reload classes both in Editor and with the game running in Play In Editor Mode
- Automatically bind BlueprintCallable UFunctions from the engine and from third party plugins when you compile the C++ Project
- Full syntax highlighting and code-hinting with the Visual Studio Code Extension
-----------

# Sample Code

```angelscript
class ATestActor : AAngelscriptActor
{
    UPROPERTY()
    float speed = 3.0;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Print("Hi, I am an Actor");
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        FVector dir = FVector(1.0, 0.0, 0.0);
        FHitResult hit;
        SetActorLocationAndRotation(GetActorLocation + dir * speed, GetActorRotation, true, hit, true);
    }
};
```
--------

# Setup
- If you haven't already, create the Plugins folder in your project's root directory
- Start by copying UNREANGEL to your project's Plugins folder
- Find your UProject file, right-click on it, and select Generate Project Files
- Open your IDE, and compile the project

- *Please Note, there is a current limitation where you will have to compile twice before the plugin works*
- *1st compilation may take several minutes and will fail because it has to update various dependencies after Unreal Build Tool has already run*
- *2nd compilation should be much faster, and should succeed since the dependencies have been properly updated*
- *Any time a new plugin is enabled or added to the project, compiling twice will be required*

- Once you have compiled you can find the VS Code Extension for Unreal Angelscript Support here: https://github.com/Hazelight/vscode-unreal-angelscript
- Now open your project, and navigate to Tools > Open Angelscript Workspace (VS Code)
- For additional information you can refer to the original fork here: https://angelscript.hazelight.se/

----------

# Work In Progress Features (Subject to Change)

- Fix the Compile Twice Problem (listed above)
- Step Through Debugging in Visual Studio Code
- Create Literal Data Assets
- Binding EditorOnly UFunctions
- Improve Bind generation for UClasses marked with MinimalAPI or No API macro
- Proper Mac and Linux support and testing
- More Unreal Engine Version Support, e.g. 5.5, 5.6

----------

