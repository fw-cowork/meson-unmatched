# UML Diagrams

This directory stores PlantUML source for the Unmatched study framework.

The diagrams document architecture and workflows. They are not configuration
inputs. Framework configuration remains in YAML files, Meson cross files, and
generated JSON metadata.

Files:

```text
repository-components.puml  repository-level component boundaries
build-sequence.puml         build orchestration sequence
data-model.puml             board, manifest, component, artifact model
artifact-flow.puml          deploy and SD-card artifact flow
component-state.puml        component lifecycle state machine
command-surface.puml        operator command and safety boundary
dependency-graph.puml       component and artifact dependencies
sd-update-sequence.puml     SD boot partition update transaction
```

Render with PlantUML if it is installed:

```bash
plantuml docs/uml/*.puml
```
