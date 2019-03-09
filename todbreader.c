/*
Copyright 2019 toutils
https://github.com/toutils/todbreader

This file is part of todbreader.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*
todbreader version 0.2
todbmanager database version 1
*/

#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <sqlite3.h>

/*NOTES
-pointers returned from sqlite are only valid until finalize or the next step
do not free

*/

GtkBuilder *builder;
sqlite3 *db=NULL;

static void apply_css_provider (GtkWidget * widget, GtkCssProvider * css_provider) {
	gtk_style_context_add_provider( gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(css_provider) , GTK_STYLE_PROVIDER_PRIORITY_USER );
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall( GTK_CONTAINER (widget), (GtkCallback)apply_css_provider , css_provider);
	}
}


//comments have newlines, this should be dealt with in the
//scraper, not here
static void remove_trailing_newline(char *str){
	if (str==NULL) { return; }
	int length=strlen(str);

	if ( str[length-1]=='\n' || str[length-1]=='\r' ) { str[length-1]='\0';  }
	return;
}

//get column text, clean it, create new label with it, return label
GtkWidget *new_label_from_sqlite3_column( sqlite3_stmt *stmt, int index ) {
	if ( sqlite3_column_bytes(stmt,index)==0 ) {
		return NULL; 
	}

	char *linebuffer;
	GtkWidget *label;

	linebuffer=malloc( sqlite3_column_bytes(stmt,index)+1 );
	strcpy(linebuffer, sqlite3_column_text(stmt, index) );
	remove_trailing_newline (linebuffer);
	label=gtk_label_new( linebuffer );
	free(linebuffer);
	return label;
}

//checks and logs sqlite errors
static void check_error(int rc, sqlite3 *sb) {
	if (rc != SQLITE_OK) {
		g_print("SQL ERROR:%d %s\n",rc, sqlite3_errmsg(db));
	}
}

//maybe more here later, logging, etc
static void set_statusbar_text(char *text) {
	GObject *label_status=gtk_builder_get_object(builder, "label_status");
	gtk_label_set_text( GTK_LABEL(label_status), text);

	//this should be handled with threading
	//hold execution until the status bar is updated, otherwise gtk will
	//not update the widget until idle
	gtk_widget_queue_draw(GTK_WIDGET(label_status));  
	while (g_main_context_pending(NULL)) {
    	g_main_context_iteration(NULL,FALSE);
	}
}

static void show_error_dialog(char *text) {
	GObject *parent_window=gtk_builder_get_object(builder, "window");

	GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
	GtkWidget *error_dialog=gtk_message_dialog_new( GTK_WINDOW(parent_window), 
		flags, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s",text);
	gtk_dialog_run(GTK_DIALOG(error_dialog));
	gtk_widget_destroy(error_dialog);
}

//create todbreader indexes if they don't exist
static void create_indexes() {
	int rc; //sqlite response code
	int rc_comment;
	sqlite3_stmt *stmt;


	g_print("recreating stats table indexes if needed...\n");
	set_statusbar_text("recreating stats table indexes if needed...");

	//covering index not needed, sqlite autoindex used for unique field
	/*
	rc=sqlite3_prepare_v2(db,
		"CREATE INDEX IF NOT EXISTS todbreader_stats_requester_id_covering ON"
		" stats (requester_id, fair, fast, pay,"
		" comm, tosviol, numreviews);"
		,-1, &stmt, NULL);
	rc=sqlite3_step(stmt);
	rc=sqlite3_finalize(stmt);
	*/

	rc=sqlite3_prepare_v2(db,
		"CREATE INDEX IF NOT EXISTS todbreader_stats_requester_name_covering ON"
		" stats (requester_name, fair, fast, pay,"
		" comm, tosviol, numreviews);"
		,-1, &stmt, NULL);
	rc=sqlite3_step(stmt);
	rc=sqlite3_finalize(stmt);

	g_print("recreating review table indexes if needed...\n");
	set_statusbar_text("recreating review table indexes if needed...");

	
	rc=sqlite3_prepare_v2(db,
		"CREATE INDEX IF NOT EXISTS todbreader_reviews_requester_id_covering ON"
		" reviews (requester_id, date, requester_name,"
		" fair, fast, pay, comm, review, notes, tosviol);"
		,-1, &stmt, NULL);
	rc=sqlite3_step(stmt);
	rc=sqlite3_finalize(stmt);
	

	rc=sqlite3_prepare_v2(db,
		"CREATE INDEX IF NOT EXISTS todbreader_reviews_requester_name_covering ON"
		" reviews (requester_name, date, requester_id,"
		" fair, fast, pay, comm, review, notes, tosviol);"
		,-1, &stmt, NULL);
	rc=sqlite3_step(stmt);
	rc=sqlite3_finalize(stmt);

	g_print("recreating comment table indexes if needed...\n");
	set_statusbar_text("recreating review comment indexes if needed...");
	rc=sqlite3_prepare_v2(db,
		"CREATE INDEX IF NOT EXISTS todbreader_comments_covering ON"
		" comments (p_key_review, date, comment, notes, type);"
		,-1, &stmt, NULL);
	rc=sqlite3_step(stmt);
	rc=sqlite3_finalize(stmt);
}

static void search(GtkWidget *widget, gpointer data) {
	if (db==NULL) {
		show_error_dialog("No database connected");
		return;
	}

	//this probably isn't a great way to do this
	//if called from next_page / prev_page, data will be non-null
	//reset the current page back to 1
	int current_page;
	if (data!=NULL) {
		current_page=1;
		//update the label
		//char current_page_buffer[10];
		//sprintf(current_page_buffer, "%i", current_page);
		gtk_label_set_text( GTK_LABEL( gtk_builder_get_object(builder, "label_current_page") ), "1");

	}
	else {
		current_page=atoi ( gtk_label_get_text( 
			GTK_LABEL( gtk_builder_get_object(builder, "label_current_page") )));
	}
	g_print("Current Page:%i\n", current_page);

	int limit=20; //this needs to be an option somewhere

	//calculate offset
	int offset=(current_page*limit)-limit;
	g_print("Offset:%i\n", offset);
	
	int total_results=0;
	int total_pages=0;
	int total_tos=0;

	//don't free query
	const gchar *query=gtk_entry_get_text(
		GTK_ENTRY(gtk_builder_get_object(builder, "entry_search")));
	//free search_type with g_free
	gchar *search_type=gtk_combo_box_text_get_active_text(
		GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "combo_search_type")));

	GObject *grid_results=gtk_builder_get_object(builder, "grid_results");

	//review search
	int rc; //sqlite response code
	int rc_comment;
	sqlite3_stmt *stmt;
	sqlite3_stmt *stmt_comment;
	sqlite3_stmt *stmt_stats_counter;
	
	g_print("Query:%s\n",query);
	g_print("Search Type:%s\n",search_type);

	//strcmp returns 0 if match
	if (!strcmp(search_type, "Requester ID")) {
		rc=sqlite3_prepare_v2(db,
			"SELECT rowid,requester_name, requester_id, date, fair,fast,pay,comm,review,notes,tosviol"
			" FROM reviews WHERE requester_id=? ORDER BY date DESC LIMIT ? OFFSET ?;"
			,-1, &stmt, NULL);
		rc=sqlite3_prepare_v2(db,
			"SELECT fair,fast,pay,comm,tosviol,numreviews"
			" FROM stats WHERE requester_id=?;"
			,-1, &stmt_stats_counter, NULL);
	}
	else if (!strcmp(search_type, "Requester Name")) {
		g_print("requester name search prepared\n");
		rc=sqlite3_prepare_v2(db,
			"SELECT rowid,requester_name, requester_id, date, fair,fast,pay,comm,review,notes,tosviol"
			" FROM reviews WHERE requester_name=? ORDER BY date DESC LIMIT ? OFFSET ?;"
			,-1, &stmt, NULL);
		rc=sqlite3_prepare_v2(db,
			"SELECT fair,fast,pay,comm,tosviol,numreviews"
			" FROM stats WHERE requester_name=?;"
			,-1, &stmt_stats_counter, NULL);
	}
	//else should never happen

	char limit_buffer[10];
	char offset_buffer[10];
	sprintf(limit_buffer, "%i", limit);
	sprintf(offset_buffer, "%i", offset);

	rc=sqlite3_bind_text(stmt, 1, query, -1, NULL);
	check_error(rc, db); 
	rc=sqlite3_bind_text(stmt, 2, limit_buffer, -1, NULL);
	check_error(rc, db); 
	rc=sqlite3_bind_text(stmt, 3, offset_buffer, -1, NULL);
	check_error(rc, db); 
	rc=sqlite3_bind_text(stmt_stats_counter, 1, query, -1, NULL);
	check_error(rc, db); 

	g_print( "review sql:\n%s\n", sqlite3_expanded_sql(stmt) );
	
	float stats_average[4]={ 0,0,0,0 };

	if (!strcmp(search_type, "Requester ID")) {
		rc=sqlite3_step(stmt_stats_counter);
		if (rc==SQLITE_ROW) {
			for (int i=0; i<4; i++) {
				stats_average[i]=(float)sqlite3_column_double(stmt_stats_counter,i);
			}
			total_tos=sqlite3_column_int(stmt_stats_counter,4);
			total_results=sqlite3_column_int(stmt_stats_counter,5);
			g_print("requester id numreviews:%i",total_results);
		}
	}
	//the stats table can't be used for requester_name, instead, generate an
	//average of stats averages across the multiple requester id's
	//generating stats without the stats table (for average across all reviews
	//using a requester name, takes too long. 
	else if (!strcmp(search_type, "Requester Name")) {
		rc=sqlite3_step(stmt_stats_counter);
		int total_requester_ids=0;
		while (rc==SQLITE_ROW) {
			total_requester_ids+=1;
			for (int i=0; i<4; i++) {
				stats_average[i]+=(float)sqlite3_column_double(stmt_stats_counter,i);
			}
			total_tos+=sqlite3_column_int(stmt_stats_counter,4);
			total_results+=sqlite3_column_int(stmt_stats_counter,5);
			rc=sqlite3_step(stmt_stats_counter);
		}
		//calculate average of requester averages
		if (total_requester_ids==0) { total_results=0; }
		else {
			for (int i=0; i<4; i++) {
				stats_average[i]=stats_average[i]/total_requester_ids;
			}
		}
	}
	g_free(search_type);
	
	total_pages=(total_results/limit);
	if ( (total_results % limit) !=0 ) {
		total_pages+=1;
	}
	g_print("total pages:%i\n", total_pages);
	char total_pages_buffer[20];
	sprintf(total_pages_buffer, "%i", total_pages);
	gtk_label_set_text( GTK_LABEL(gtk_builder_get_object(builder, "label_total_pages")), total_pages_buffer );

	//check that this isn't actually causing a memory leak
	gtk_container_foreach(GTK_CONTAINER(grid_results), (GtkCallback)gtk_widget_destroy, NULL);

	GtkWidget *grid_stats;
	GtkWidget *grid_review;
	GtkWidget *grid_comment;
	GtkWidget *grid_comments;
	GtkWidget *frame_comments;
	GtkWidget *level_bar;
	GtkWidget *label_ptr;

	char stats_buffer_array_labels[4][40] = 
		{ "Fair:\t\t\0", "Fast:\t\t\0", "Pay:\t\t\0", "Comm:\t\0" };
	char stats_buffer[200];

	int result_count=0;
	int comment_count=0;

	//stat average grid
	GtkWidget *grid_stats_average = gtk_grid_new();
	for (int i=0; i<4; i++) {
		if ( stats_average[i]==0 ) {
			sprintf( stats_buffer, "  %s%s", stats_buffer_array_labels[i],"NONE");
		}
		else {
			sprintf( stats_buffer, "  %s%.2f", stats_buffer_array_labels[i],
				stats_average[i] );
		}
		label_ptr=gtk_label_new( stats_buffer );
		g_object_set (label_ptr, "hexpand", TRUE, NULL);
		stats_buffer[0]='\0';
		gtk_widget_set_name( GTK_WIDGET(label_ptr), "stats_label");
		gtk_grid_attach(GTK_GRID(grid_stats_average), label_ptr, i, 0, 1, 1);

		level_bar=gtk_level_bar_new();
		g_object_set (level_bar, "hexpand", TRUE, NULL);
		gtk_widget_set_name( GTK_WIDGET(level_bar), "stats_levelbar" );
		gtk_level_bar_set_max_value ( GTK_LEVEL_BAR(level_bar), 5);
		gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "lowest", 1);
		gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "low", 2);
		gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "mid", 3.9);
		gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "high", 4);
		gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "full", 5);

		gtk_level_bar_set_value( GTK_LEVEL_BAR(level_bar), stats_average[i] );
		gtk_grid_attach( GTK_GRID(grid_stats_average), level_bar, i, 0, 1, 1);
	}
	gtk_grid_attach(GTK_GRID(grid_results),grid_stats_average, 0, result_count, 1, 1);
	result_count+=1;

	//total reviews/tosviolations
	sprintf( stats_buffer, "Reviews:%i TOS Violations:%i", 
		total_results, total_tos);
	label_ptr=gtk_label_new(stats_buffer);
	gtk_grid_attach(GTK_GRID(grid_results),label_ptr, 0, result_count, 1, 1);
	result_count+=1;

	rc=sqlite3_step(stmt);

	while(rc==SQLITE_ROW) {
		total_results+=1;
		//STATS GRID
		grid_stats=gtk_grid_new();

		//stats levelbars w/ label overlay
		for (int i=0; i<4; i++) {
			//sprintf( stats_buffer_array[i], sqlite3_column_text(stmt, 4+i) );
			//some of these will return null and strcat will fail
			if ( sqlite3_column_text(stmt, 4+i)==NULL ) {
				sprintf( stats_buffer, "  %s%s", stats_buffer_array_labels[i],"NONE");
			}
			else {
				sprintf( stats_buffer, "  %s%s", stats_buffer_array_labels[i],
					sqlite3_column_text(stmt, 4+i) );
			}
			label_ptr=gtk_label_new( stats_buffer );
			stats_buffer[0]='\0';
			gtk_widget_set_name( GTK_WIDGET(label_ptr), "stats_label");
			gtk_label_set_xalign( GTK_LABEL(label_ptr), 0.0f );
			gtk_widget_set_size_request ( GTK_WIDGET(label_ptr), 120,20);
			gtk_grid_attach(GTK_GRID(grid_stats), label_ptr, 0, 3+i, 1, 1);

			level_bar=gtk_level_bar_new();
			gtk_widget_set_name( GTK_WIDGET(level_bar), "stats_levelbar" );
			gtk_level_bar_set_max_value ( GTK_LEVEL_BAR(level_bar), 5);
			gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "lowest", 1);
			gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "low", 2);
			gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "mid", 3);
			gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "high", 4);
			gtk_level_bar_add_offset_value( GTK_LEVEL_BAR(level_bar), "full", 5);
			gtk_widget_set_size_request ( GTK_WIDGET(level_bar), 120,20);

			gtk_level_bar_set_value( GTK_LEVEL_BAR(level_bar), sqlite3_column_int(stmt, 4+i) );
			gtk_grid_attach( GTK_GRID(grid_stats), level_bar, 0, 3+i, 1, 1);
		}
		
		//requester name/id, date
		for (int i=0; i<3; i++) {
			label_ptr=gtk_label_new( sqlite3_column_text(stmt, 1+i) );
			gtk_label_set_xalign( GTK_LABEL(label_ptr), 0.0f );
			gtk_label_set_selectable ( GTK_LABEL(label_ptr), TRUE);
			gtk_grid_attach(GTK_GRID(grid_stats), label_ptr, 0, i, 1, 1);
		}
		//tos violation
		if ( sqlite3_column_int(stmt, 10) == 1 ) {
			label_ptr=gtk_label_new("TOS VIOLATION");
			gtk_label_set_xalign( GTK_LABEL(label_ptr), 0.0f );
			gtk_grid_attach(GTK_GRID(grid_stats), label_ptr, 0, 7, 1, 1);
		}
		
		//REVIEW GRID
		//review/notes
		grid_review=gtk_grid_new();
		for(int i=0; i<2; i++) {
			label_ptr=new_label_from_sqlite3_column(stmt, 8+i);
			if (label_ptr) {
				gtk_label_set_xalign( GTK_LABEL(label_ptr), 0.0f );
				gtk_label_set_line_wrap( GTK_LABEL(label_ptr), TRUE);
				gtk_label_set_line_wrap_mode (GTK_LABEL (label_ptr), PANGO_WRAP_WORD_CHAR);
				gtk_label_set_selectable ( GTK_LABEL(label_ptr), TRUE);
				gtk_grid_attach(GTK_GRID(grid_review),label_ptr, 0, i, 1, 1);
			}
		}

		//COMMENT GRID
		rc_comment=sqlite3_prepare_v2(db,
			"SELECT comment, notes, date, type FROM comments WHERE p_key_review=? ORDER BY date DESC;"
			,-1, &stmt_comment, NULL);
		rc_comment=sqlite3_bind_int(stmt_comment, 1, sqlite3_column_int(stmt, 0));
		rc_comment=sqlite3_step(stmt_comment);
	
		grid_comments=gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(grid_comments), 4);

		while(rc_comment==SQLITE_ROW) {
			grid_comment=gtk_grid_new();

			//comment/notes
			for (int i=0; i<4; i++) {
				label_ptr=new_label_from_sqlite3_column(stmt_comment, i);
				if (label_ptr) {
					gtk_label_set_xalign( GTK_LABEL(label_ptr), 0.0f );
					gtk_label_set_line_wrap( GTK_LABEL(label_ptr), TRUE);
					gtk_label_set_line_wrap_mode (GTK_LABEL (label_ptr), PANGO_WRAP_WORD_CHAR);
					gtk_label_set_selectable ( GTK_LABEL(label_ptr), TRUE);
					gtk_grid_attach(GTK_GRID(grid_comment),label_ptr, 0, i, 1, 1);	
				}
			}
			gtk_grid_attach(GTK_GRID(grid_comments), grid_comment, 0, comment_count, 1, 1);
			comment_count+=1;
			rc_comment=sqlite3_step(stmt_comment);
		}
		rc_comment=sqlite3_finalize(stmt_comment);

		frame_comments=gtk_frame_new("Comments");
		g_object_set (frame_comments, "hexpand", TRUE, NULL);
		gtk_container_add( GTK_CONTAINER(frame_comments), grid_comments);
		gtk_grid_attach( GTK_GRID(grid_review), frame_comments, 0, 2, 1, 1);

		gtk_grid_set_row_spacing( GTK_GRID(grid_review), 4 );

		GtkWidget *grid_result=gtk_grid_new();
		gtk_grid_attach( GTK_GRID(grid_result), grid_stats, 0, 0, 1, 1);
		gtk_grid_attach( GTK_GRID(grid_result), grid_review, 1, 0, 1, 1);
		gtk_grid_set_column_spacing(GTK_GRID(grid_result), 6);

		GtkWidget *frame_result=gtk_frame_new(NULL);
		gtk_container_add ( GTK_CONTAINER(frame_result), grid_result);
		gtk_widget_set_name( frame_result, "bordered_heavy" );
		
		gtk_grid_attach(GTK_GRID(grid_results),frame_result, 0, result_count, 1, 1);

		result_count+=1;
		rc=sqlite3_step(stmt);
	}

	rc=sqlite3_finalize(stmt);
	check_error(rc, db);
	gtk_grid_set_row_spacing(GTK_GRID(grid_results), 6);
	gtk_widget_show_all(GTK_WIDGET(grid_results));
}

static void next_page(GtkWidget *widget, gpointer data) {
	GObject *label_current_page=gtk_builder_get_object(builder, "label_current_page");
	GObject *label_total_pages=gtk_builder_get_object(builder, "label_total_pages");

	int current_page=atoi (gtk_label_get_text(GTK_LABEL( label_current_page )));
	int total_pages=atoi (gtk_label_get_text(GTK_LABEL( label_total_pages )));

	current_page+=1;
	if (current_page>total_pages) { return; }

	char buffer[10];
	sprintf(buffer, "%i", current_page);
	gtk_label_set_text( GTK_LABEL( label_current_page ), buffer );

	search( NULL, NULL);
}

static void prev_page(GtkWidget *widget, gpointer data) {
	GObject *label_current_page=gtk_builder_get_object(builder, "label_current_page");

	int current_page=atoi (gtk_label_get_text(GTK_LABEL( label_current_page )));
	current_page-=1;
	if (current_page<1) { return; }
	char buffer[10];
	sprintf(buffer, "%i", current_page);
	gtk_label_set_text( GTK_LABEL( label_current_page ), buffer );

	search( NULL, NULL);
}


static void load_database(GtkWidget *widget, gpointer data) {
	//get the filepath
	GtkFileChooserNative *native;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint res;
	int rc; //sqlite response code
	sqlite3_stmt *stmt; 

	GObject *parent_window=gtk_builder_get_object(builder, "window");

	native=gtk_file_chooser_native_new("Open File", GTK_WINDOW(parent_window), action,
		"_Open", "_Cancel");

	res=gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
	
	if (res !=GTK_RESPONSE_ACCEPT) {
		g_object_unref(native);
		return;
	}

	char *filepath; //free with g_free
	GtkFileChooser *chooser = GTK_FILE_CHOOSER(native);
	filepath=gtk_file_chooser_get_filename(chooser);

	g_print("Opening database:%s\n",filepath);

	rc=sqlite3_open(filepath, &db);
	if (rc) {
		g_print("Error opening database: %s\n", sqlite3_errmsg(db));
	}
	else {
		g_print("Database opened\n");
	}

	//check if valid database
	//this will return an integer or error out, don't care about version
	//need to check for sqlite errors on db when running query
	//ie attempting to load something that isn't a database at all
	sqlite3_prepare_v2(db, "pragma schema_version;", -1, &stmt, NULL);
	rc=sqlite3_step(stmt);
	//if it returns a row it's probably good
	if (rc==SQLITE_ROW) {
		g_print("valid database\n");
		sqlite3_finalize(stmt);
	}
	//this is what is being looked for. 
	else {
		char error_message[100];
		if (rc==SQLITE_NOTADB) {
			sprintf(error_message,"%s\nInvalid database:SQLITE_NOTADB",filepath);
		}
		else {
			sprintf(error_message,"%s\nInvalid database:unhandled rc:%i",filepath,rc);
		}
		g_print("%s\n",error_message);
		show_error_dialog(error_message);

		//cleanup
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		db=NULL;
	}
	g_free(filepath);

	//create the indexes if needed
	create_indexes();

	//get reviews and comments counts
	sqlite3_prepare_v2(db, "SELECT Count(*) FROM reviews;",-1, &stmt, NULL);
	sqlite3_step(stmt);
	int review_count=sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);

	sqlite3_prepare_v2(db, "SELECT Count(*) FROM comments;",-1, &stmt, NULL);
	sqlite3_step(stmt);
	int comment_count=sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);

	char update_message[100];
	sprintf(update_message,"%i reviews, %i comments available",review_count,comment_count);
	set_statusbar_text(update_message);
	
	g_object_unref(native);
}

int main (int argc, char **argv)
{
	GObject *window;
	GObject *btn_load;
	GObject *btn_search;
	GObject *btn_next;
	GObject *btn_prev;

	//css styling
	GtkCssProvider* css_provider;

	GError *error = NULL;

	gtk_init(&argc, &argv);

	//CSS
	css_provider = gtk_css_provider_new();
	gtk_css_provider_load_from_path(css_provider, "theme.css", NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                               GTK_STYLE_PROVIDER(css_provider),
                               GTK_STYLE_PROVIDER_PRIORITY_USER);
	
	builder=gtk_builder_new();
	if(gtk_builder_add_from_file(builder, "builder.ui", &error) == 0)
	{
		g_printerr("Error loading file %s\n", error->message);
		g_clear_error(&error);
		return 1;
	}

	window=gtk_builder_get_object(builder, "window");
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	apply_css_provider( GTK_WIDGET(window) , css_provider );

	btn_load=gtk_builder_get_object(builder, "btn_load");
	g_signal_connect(btn_load, "clicked", G_CALLBACK(load_database), NULL);

	btn_search=gtk_builder_get_object(builder, "btn_search");
	//anything non-null to gpointer data will reset current_page to 1
	g_signal_connect(btn_search, "clicked", G_CALLBACK(search), "reset");

	btn_next=gtk_builder_get_object(builder, "btn_next");
	g_signal_connect(btn_next, "clicked", G_CALLBACK(next_page), NULL);

	btn_prev=gtk_builder_get_object(builder, "btn_prev");
	g_signal_connect(btn_prev, "clicked", G_CALLBACK(prev_page), NULL);

	gtk_main();

	if ( db!=NULL ) {
		sqlite3_close(db);
	}

	return 0;
}
