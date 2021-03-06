// Source file for the scene converter program



// Include files 

#include "R3Graphics/R3Graphics.h"



// Program arguments

static const char *input_name = NULL;
static const char *output_name = NULL;
static char *input_categories_name = NULL;
static char *input_lights_name = NULL;
static const char *select_nodes_in_subtree_name = NULL;
static R3Box select_nodes_in_bbox(FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
static R3Affine xform(R4Matrix(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1), 0);
static int remove_references = 0;
static int remove_hierarchy = 0;
static int remove_transformations = 0;
static RNLength max_edge_length = 0;
static int print_verbose = 0;



////////////////////////////////////////////////////////////////////////
// I/O STUFF
////////////////////////////////////////////////////////////////////////

static R3Scene *
ReadScene(const char *filename)
{
  // Start statistics
  RNTime start_time;
  start_time.Read();

  // Allocate scene
  R3Scene *scene = new R3Scene();
  if (!scene) {
    fprintf(stderr, "Unable to allocate scene for %s\n", filename);
    return NULL;
  }

  // Read scene from file
  if (!scene->ReadFile(filename)) {
    delete scene;
    return NULL;
  }

  // Print statistics
  if (print_verbose) {
    printf("Read scene from %s ...\n", filename);
    printf("  Time = %.2f seconds\n", start_time.Elapsed());
    printf("  # Nodes = %d\n", scene->NNodes());
    printf("  # Lights = %d\n", scene->NLights());
    printf("  # Materials = %d\n", scene->NMaterials());
    printf("  # Brdfs = %d\n", scene->NBrdfs());
    printf("  # Textures = %d\n", scene->NTextures());
    printf("  # Referenced models = %d\n", scene->NReferencedScenes());
    fflush(stdout);
  }

  // Return scene
  return scene;
}



static int
WriteScene(R3Scene *scene, const char *filename)
{
  // Start statistics
  RNTime start_time;
  start_time.Read();

  // Write scene from file
  if (!scene->WriteFile(filename)) return 1;

  // Print statistics
  if (print_verbose) {
    printf("Wrote scene to %s ...\n", filename);
    printf("  Time = %.2f seconds\n", start_time.Elapsed());
    printf("  # Nodes = %d\n", scene->NNodes());
    printf("  # Lights = %d\n", scene->NLights());
    printf("  # Materials = %d\n", scene->NMaterials());
    printf("  # Brdfs = %d\n", scene->NBrdfs());
    printf("  # Textures = %d\n", scene->NTextures());
    printf("  # Referenced models = %d\n", scene->NReferencedScenes());
    fflush(stdout);
  }

  // Return success
  return 1;
}



static int
ReadCategories(R3Scene *scene, const char *filename)
{
  // Start statistics
  RNTime start_time;
  start_time.Read();

  // Read file
  if (!scene->ReadSUNCGModelFile(filename)) return 0;

  // Print statistics
  if (print_verbose) {
    printf("Read categories from %s ...\n", filename);
    printf("  Time = %.2f seconds\n", start_time.Elapsed());
    fflush(stdout);
  }

  // Return success
  return 1;
} 



static int
ReadLights(R3Scene *scene, const char *filename)
{
  // Start statistics
  RNTime start_time;
  start_time.Read();

  // Read lights file
  if (!scene->ReadSUNCGLightsFile(filename)) return 0;

  // Print statistics
  if (print_verbose) {
    printf("Read lights from %s ...\n", filename);
    printf("  Time = %.2f seconds\n", start_time.Elapsed());
    printf("  # Lights = %d\n", scene->NLights());
    fflush(stdout);
  }

  // Return success
  return 1;
}
  


static int
ReadMatrix(R4Matrix& m, const char *filename)
{
  // Open file
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "Unable to open matrix file: %s\n", filename);
    return 0;
  }

  // Read matrix from file
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      double value;
      fscanf(fp, "%lf", &value);
      m[i][j] = value;
    }
  }

  // Close file
  fclose(fp);

  // Return success
  return 1;
}



////////////////////////////////////////////////////////////////////////
// PROCESSING
////////////////////////////////////////////////////////////////////////

static void
RemoveNodes(R3Scene *scene, R3SceneNode *node,
  R3SceneNode *select_subtree_node, const R3Box& select_bbox)
{
  // Copy array of children -- because will be edited as iterate
  RNArray<R3SceneNode *> children;
  for (int i = 0; i < node->NChildren(); i++) {
    R3SceneNode *child = node->Child(i);
    children.Insert(child);
  }

  // Visit children recursively in post order
  for (int i = 0; i < children.NEntries(); i++) {
    R3SceneNode *child = children.Kth(i);
    RemoveNodes(scene, child, select_subtree_node, select_bbox);
  }

  // Innocent until proven guilty
  RNBoolean remove = FALSE;

  // Check if should remove based on bbox selection
  if (!select_bbox.IsEmpty()) {
    if (!R3Intersects(node->WorldBBox(), select_bbox)) {
      remove = TRUE;
    }
  }

  // Check if should remove based on subtree selection
  if (select_subtree_node) {
    if (node != select_subtree_node) {
      if (!node->IsAncestor(select_subtree_node)) { 
        if (!node->IsDecendent(select_subtree_node)) {
          remove = TRUE;
        }
      }
    }
  }

  // Check if should parden based on topology
  if (node->NChildren() > 0) remove= FALSE;
  if (node == scene->Root()) remove = FALSE;

  // Delete node 
  if (remove) delete node;
}



static int
RemoveNodes(R3Scene *scene)
{
  // Check if selecting nodes
  if (select_nodes_in_bbox.IsEmpty() && !select_nodes_in_subtree_name) return 1;

  // Find select subtree node
  R3SceneNode *select_subtree_node = NULL;
  if (select_nodes_in_subtree_name) {
    select_subtree_node = scene->Node(select_nodes_in_subtree_name);
    if (!select_subtree_node) {
      fprintf(stderr, "Unable to find select subtree node %s\n", select_nodes_in_subtree_name);
      return 0;
    }
  }

  // Remove nodes recursively in post order
  RemoveNodes(scene, scene->Root(), select_subtree_node, select_nodes_in_bbox);

  // Return success
  return 1;
}



////////////////////////////////////////////////////////////////////////
// PROGRAM ARGUMENT PARSING
////////////////////////////////////////////////////////////////////////

static int
ParseArgs(int argc, char **argv)
{
  // Parse arguments
  argc--; argv++;
  while (argc > 0) {
    if ((*argv)[0] == '-') {
      R3Affine prev_xform = xform;
      if (!strcmp(*argv, "-v")) print_verbose = 1;
      else if (!strcmp(*argv, "-remove_references")) remove_references = 1;
      else if (!strcmp(*argv, "-remove_hierarchy")) remove_hierarchy = 1;
      else if (!strcmp(*argv, "-remove_transformations")) remove_transformations = 1;
      else if (!strcmp(*argv, "-select_nodes_in_subtree")) {
        argv++; argc--; select_nodes_in_subtree_name = *argv;
      }
      else if (!strcmp(*argv, "-select_nodes_in_bbox")) {
        argv++; argc--; select_nodes_in_bbox[0][0] = atof(*argv);
        argv++; argc--; select_nodes_in_bbox[0][1] = atof(*argv);
        argv++; argc--; select_nodes_in_bbox[0][2] = atof(*argv);
        argv++; argc--; select_nodes_in_bbox[1][0] = atof(*argv);
        argv++; argc--; select_nodes_in_bbox[1][1] = atof(*argv);
        argv++; argc--; select_nodes_in_bbox[1][2] = atof(*argv);
      }
      else if (!strcmp(*argv, "-scale")) {
        argv++; argc--; xform.Scale(atof(*argv));
      }
      else if (!strcmp(*argv, "-tx")) {
        argv++; argc--; xform = R3identity_affine; xform.XTranslate(atof(*argv)); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-ty")) {
        argv++; argc--; xform = R3identity_affine; xform.YTranslate(atof(*argv)); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-tz")) {
        argv++; argc--; xform = R3identity_affine; xform.ZTranslate(atof(*argv)); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-sx")) {
        argv++; argc--; xform = R3identity_affine; xform.XScale(atof(*argv)); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-sy")) {
        argv++; argc--; xform = R3identity_affine; xform.YScale(atof(*argv)); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-sz")) {
        argv++; argc--; xform = R3identity_affine; xform.ZScale(atof(*argv)); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-rx")) {
        argv++; argc--; xform = R3identity_affine; xform.XRotate(RN_PI*atof(*argv)/180.0); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-ry")) {
        argv++; argc--; xform = R3identity_affine; xform.YRotate(RN_PI*atof(*argv)/180.0); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-rz")) {
        argv++; argc--; xform = R3identity_affine; xform.ZRotate(RN_PI*atof(*argv)/180.0); xform.Transform(prev_xform);
      }
      else if (!strcmp(*argv, "-xform")) {
        argv++; argc--; R4Matrix m;  if (ReadMatrix(m, *argv)) { xform = R3identity_affine; xform.Transform(R3Affine(m)); xform.Transform(prev_xform);}
      } 
      else if (!strcmp(*argv, "-max_edge_length")) {
        argv++; argc--; max_edge_length = atof(*argv);
      }
      else if (!strcmp(*argv, "-categories")) {
        argc--; argv++; input_categories_name = *argv;
      }
      else if (!strcmp(*argv, "-lights")) {
        argv++; argc--; input_lights_name = *argv;
      }
      else {
        fprintf(stderr, "Invalid program argument: %s", *argv);
        exit(1);
      }
      argv++; argc--;
    }
    else {
      if (!input_name) input_name = *argv;
      else if (!output_name) output_name = *argv;
      else { fprintf(stderr, "Invalid program argument: %s", *argv); exit(1); }
      argv++; argc--;
    }
  }

  // Check input filename
  if (!input_name || !output_name) {
    fprintf(stderr, "Usage: scn2scn inputfile outputfile [options]\n");
    return 0;
  }

  // Return OK status 
  return 1;
}



////////////////////////////////////////////////////////////////////////
// MAIN
////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
  // Check number of arguments
  if (!ParseArgs(argc, argv)) exit(1);

  // Read scene
  R3Scene *scene = ReadScene(input_name);
  if (!scene) exit(-1);

  // Read categories
  if (input_categories_name) {
    if (!ReadCategories(scene, input_categories_name)) exit(-1);
  }

  // Read lights
  if (input_lights_name) {
    if (!ReadLights(scene, input_lights_name)) exit(-1);
  }

  // Remove unselected nodes
  if (!select_nodes_in_bbox.IsEmpty() || select_nodes_in_subtree_name) {
    if (!RemoveNodes(scene)) exit(-1);
  }

  // Transform scene
  if (!xform.IsIdentity()) {
    R3SceneNode *root = scene->Root();
    R3Affine tmp = R3identity_affine;
    tmp.Transform(xform);
    tmp.Transform(root->Transformation());
    root->SetTransformation(tmp);
  }

  // Apply processing operations
  if (remove_references) scene->RemoveReferences();
  if (remove_hierarchy) scene->RemoveHierarchy();
  if (remove_transformations) scene->RemoveTransformations();
  if (max_edge_length > 0) scene->SubdivideTriangles(max_edge_length);

  // Write scene
  if (!WriteScene(scene, output_name)) exit(-1);

  // Return success 
  return 0;
}

















