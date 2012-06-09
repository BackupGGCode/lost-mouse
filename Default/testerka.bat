@echo off
cls
echo **********************************************
echo *Program uruchamiajacy aplikacje "lost-mouse"*
echo *  wykonana w ramach projektu z przedmiotu   *
echo *          inferfesjy multimodlane           *
echo **********************************************
echo *                  Autorzy:                  *
echo *                                            *
echo *               Joanna Kulesza               *
echo *                Kamil Lopata                *
echo *              Zbigniew Tekiela              *
echo *                                            *
echo **********************************************
echo *         Akademia Gorniczo-Hutnicza         *
echo *     im. Stanislawa Staszica w Krakowie     *
echo *                                            *
echo *                Wydzial EAIiE               *
echo *            Informatyka Stosowana           *
echo *                    2012                    *
echo *                                            *
echo **********************************************
echo.
echo.
echo Standardowa konfiguracja: obraz z kamery, automatyczne pobieranie modelu koloru skory
set input = 0
set /p runopt= Odpalic program uzywajac standarowych ustawien ? (T/N)

if /I %runopt%==T ( goto defaultrun) else (goto advancedrun)

:advancedrun
set /a c=1

setlocal ENABLEDELAYEDEXPANSION

echo Jakie chcesz zrodlo sygnalu video?
echo 0: kamera
dir /s /b *.avi > videos.txt
for /f "tokens=1 delims=" %%i in (videos.txt) do (
	echo !c!: %%i
	set /a c=c+1
) 

:wyborzrodla
set /p uinput= Ktore zrodlo sygnalu wybierasz? 
set /a input=uinput
if !input! geq 0 (
	if !input! LSS !c! ( goto dobrezrodlo ) else ( goto zlezrodlo )
)

:zlezrodlo
echo Zly wybor!
goto wyborzrodla

:dobrezrodlo
set /a c=1
if !input! equ 0 ( set zrodlo=null
		   echo Wybrales kamere) else (
	for /f "tokens=1 delims=" %%j in (videos.txt) do (
		if !input! equ !c! ( set zrodlo=%%j) 
		set /a c=c+1
	) 
echo Wybrales %zrodlo%
)


:wyborzaznaczenia
set /p auto= Czy obszar ma byc zaznaczany automatycznie? (T/N) 

if /I %auto%==T ( set auto=true ) else (set auto=false)

echo.
echo.
echo.
echo lost-mouse.exe %zrodlo% %auto%
echo.
echo.
echo.
lost-mouse.exe %zrodlo% %auto%
goto end

:defaultrun
echo.
echo.
echo.
echo lost-mouse.exe
echo.
echo.
echo.
lost-mouse.exe
goto end

:end

