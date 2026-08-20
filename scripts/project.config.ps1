# Per-project defaults for light_display.
#
# HOST-ONLY for now: the only buildable target outside a consuming project is module/light_draw's
# own demo (light_draw_demo), which needs nothing but light_core and (for its generated font)
# font-crusher -- both resolvable on a plain host build. The panel drivers are INTERFACE
# libraries with nothing of their own to build or run standalone; a real target build only
# happens through a consuming project (screen-test, crossfire), via their own project.config.ps1.
@{
        Name = 'light_display'

        Trees = @{
                'conf-light_display-host-debug' = 'build'
        }

        Expect = @{
                'conf-light_display-host-debug' = @{ LIGHT_PLATFORM = 'HOST'; LIGHT_SYSTEM = 'HOST_OS'; CMAKE_BUILD_TYPE = 'Debug' }
        }

        Targets = @{
                'light_draw_demo' = @{ Preset = 'conf-light_display-host-debug' }
        }

        DefaultTarget = 'light_draw_demo'

        Test = @{
                Preset = 'conf-light_display-host-debug'
                Ctest  = $true
        }

        #   same shape as screen-test's and light_ui's, same reasons. What this measures is
        # what light_draw's geometry suite reaches -- the drawing, transform and clip code --
        # plus the framework suites the standalone build registers; the panel drivers are
        # INTERFACE-only and target-only, so they will not appear at all
        Coverage = @{
                Objects     = 'auto'
                IgnoreRegex = '(/lib/|/usr/|sanitizers/|_deps/|/freetype/|/jansson/)'
                CMakeArgs   = @('-DLIGHT_SYSTEM=HOST_OS', '-DLIGHT_PLATFORM=HOST')
        }
}
