# fkcli realeval end to end (docs/v1/REAL-SCAN-GT.md §3) on generated scans: fkfixture → fk.realgt/1 → realeval
# --enforce. Real scans stay outside the repository; this proves the tool and the gates on known geometry.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/scans")
set(gts "")
foreach(cfg "3;300;polaroid_600,instax_mini;0.06" "2;400;polaroid_600,instax_square;0.78")
  list(GET cfg 0 seed)
  list(GET cfg 1 dpi)
  list(GET cfg 2 types)
  list(GET cfg 3 bg)
  set(scan "${WORK}/scans/fixture_${seed}.tif")
  execute_process(COMMAND "${FKFIXTURE}" --out "${scan}" --gt "${WORK}/fixture_${seed}.json" --seed ${seed}
                          --dpi ${dpi} --count 2 --types ${types} --frames white --background ${bg}
                          --scan-mm 216x150 --tilt-max 2
                  RESULT_VARIABLE rc OUTPUT_QUIET)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "fkfixture failed (${rc})")
  endif()
  execute_process(COMMAND "${PYTHON}" "${CONVERT}" "${scan}" "${WORK}/fixture_${seed}.json"
                          "${WORK}/fixture_${seed}.gt.json" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "fixture-to-realgt failed (${rc})")
  endif()
  list(APPEND gts "${WORK}/fixture_${seed}.gt.json")
endforeach()
execute_process(COMMAND "${FKCLI}" realeval ${gts} --scans "${WORK}/scans" --json "${WORK}/report.json" --enforce
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "realeval gates not met on generated scans (see ${WORK}/report.json)")
endif()
