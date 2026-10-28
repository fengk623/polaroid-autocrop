# Generates the deterministic evaluation corpus (once per build tree) and enforces TEST-PLAN §4.
if(NOT EXISTS "${CORPUS_DIR}/corpus.json")
  file(REMOVE_RECURSE "${CORPUS_DIR}")
  execute_process(COMMAND "${FKFIXTURE}" --corpus ${COUNT} --seed ${SEED} --out-dir "${CORPUS_DIR}"
                  RESULT_VARIABLE rc OUTPUT_QUIET)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "fkfixture failed (${rc})")
  endif()
endif()
execute_process(COMMAND "${FKCLI}" eval "${CORPUS_DIR}" --enforce --json "${REPORT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "detection evaluation thresholds not met (see ${REPORT})")
endif()
