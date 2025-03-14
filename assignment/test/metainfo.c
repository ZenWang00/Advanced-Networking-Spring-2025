#include "unity_fixture.h"
#include "unity.h"
#include "unity_internals.h"
#include <metainfo.h>
#include <stdio.h>


TEST_GROUP(metainfo);

TEST_SETUP(metainfo)
{}

TEST_TEAR_DOWN(metainfo)
{}

TEST(metainfo, parsing)
{
    struct metainfo_file file;

    TEST_ASSERT_NOT_EQUAL(0, metainfo_file_read(&file, "test/simple.torrent"));
    metainfo_file_free(&file);
}

TEST(metainfo, announce)
{
    struct metainfo_file file;

    TEST_ASSERT_NOT_EQUAL(0, metainfo_file_read(&file, "test/simple.torrent"));
    TEST_ASSERT_EQUAL_STRING("http://tracker.com/announce", file.announce);
    metainfo_file_free(&file);

}

TEST(metainfo, info)
{
    struct metainfo_file file;
    size_t pieces;
    char buf[20];

    TEST_ASSERT_NOT_EQUAL(0, metainfo_file_read(&file, "test/simple.torrent"));
    TEST_ASSERT_EQUAL_STRING("sample.txt", file.info.name);
    TEST_ASSERT_EQUAL(32768, file.info.piece_length);
    TEST_ASSERT_EQUAL(92063, file.info.length);
    pieces = metainfo_file_pieces_count(&file);
    TEST_ASSERT_EQUAL(3, pieces);

    FILE *fp = fopen("test/simple.torrent", "rb");
    TEST_ASSERT_NOT_NULL(fp);

    fseek(fp, 144, SEEK_SET);
    
    for (size_t i = 0; i < pieces; ++i) {
	TEST_ASSERT_EQUAL(20, fread(buf, 1, 20, fp));
	TEST_ASSERT_EQUAL_MEMORY(buf, metainfo_file_piece_hash(&file, i), 20);
    }

    TEST_ASSERT_EQUAL('e', fgetc(fp));

    fclose(fp);
    metainfo_file_free(&file);
}

TEST(metainfo, ubuntu_torrent)
{
    struct metainfo_file file;
    size_t pieces;
    char buf[20];

    TEST_ASSERT_NOT_EQUAL(0, metainfo_file_read(&file, "test/ubuntu.torrent"));

    TEST_ASSERT_EQUAL_STRING("https://torrent.ubuntu.com/announce", file.announce);

    TEST_ASSERT_EQUAL_STRING("ubuntu-24.10-desktop-amd64.iso", file.info.name);
    TEST_ASSERT_EQUAL(262144, file.info.piece_length);
    TEST_ASSERT_EQUAL(5665497088, file.info.length);
    pieces = metainfo_file_pieces_count(&file);
    TEST_ASSERT_EQUAL(21613, pieces);

    FILE *fp = fopen("test/ubuntu.torrent", "rb");
    TEST_ASSERT_NOT_NULL(fp);

    fseek(fp, 354, SEEK_SET);

    for (size_t i = 0; i < pieces; ++i) {
	TEST_ASSERT_EQUAL(20, fread(buf, 1, 20, fp));
	TEST_ASSERT_EQUAL_MEMORY(buf, metainfo_file_piece_hash(&file, i), 20);
    }

    TEST_ASSERT_EQUAL('e', fgetc(fp));

    fclose(fp);
    metainfo_file_free(&file);
}

TEST(metainfo, invalid_torrent)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/invalid.torrent"));
}

TEST(metainfo, empty_file)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/empty.torrent"));
}

TEST(metainfo, not_existing_file)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/noexisting.torrent"));
}

TEST(metainfo, not_torrent_file)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/nontorrent.torrent"));
}

TEST(metainfo, invalid_announce)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/invalid_announce.torrent"));
}

TEST(metainfo, missing_announce)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/missing_announce.torrent"));
}

TEST(metainfo, missing_info)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/missing_info.torrent"));
}

TEST(metainfo, invalid_info)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/invalid_info.torrent"));
}


TEST(metainfo, missing_length)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/missing_length.torrent"));
}

TEST(metainfo, invalid_length)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/invalid_length.torrent"));
}

TEST(metainfo, negative_length)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/negative_length.torrent"));
}

TEST(metainfo, missing_piece_length)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/missing_piece_length.torrent"));
}

TEST(metainfo, invalid_piece_length)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/invalid_piece_length.torrent"));
}

TEST(metainfo, negative_piece_length)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/negative_piece_length.torrent"));
}

TEST(metainfo, missing_name)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/missing_name.torrent"));
}

TEST(metainfo, invalid_name)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/invalid_name.torrent"));
}

TEST(metainfo, missing_pieces)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/missing_pieces.torrent"));
}

TEST(metainfo, invalid_pieces)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/invalid_pieces.torrent"));
}

TEST(metainfo, pieces_not_20_multiple)
{
    struct metainfo_file file;

    TEST_ASSERT_EQUAL(0, metainfo_file_read(&file, "test/pieces_not_20_multiple.torrent"));
}



TEST_GROUP_RUNNER(metainfo)
{
    RUN_TEST_CASE(metainfo, parsing);
    RUN_TEST_CASE(metainfo, announce);
    RUN_TEST_CASE(metainfo, info);
    RUN_TEST_CASE(metainfo, ubuntu_torrent);
    RUN_TEST_CASE(metainfo, invalid_torrent);
    RUN_TEST_CASE(metainfo, empty_file);
    RUN_TEST_CASE(metainfo, not_existing_file);
    RUN_TEST_CASE(metainfo, not_torrent_file);

    RUN_TEST_CASE(metainfo, invalid_announce);
    RUN_TEST_CASE(metainfo, missing_announce);

    RUN_TEST_CASE(metainfo, missing_info);
    RUN_TEST_CASE(metainfo, invalid_info);

    RUN_TEST_CASE(metainfo, missing_length);
    RUN_TEST_CASE(metainfo, invalid_length);
    RUN_TEST_CASE(metainfo, negative_length);

    RUN_TEST_CASE(metainfo, missing_piece_length);
    RUN_TEST_CASE(metainfo, invalid_piece_length);
    RUN_TEST_CASE(metainfo, negative_piece_length);

    RUN_TEST_CASE(metainfo, missing_name);
    RUN_TEST_CASE(metainfo, invalid_name);

    RUN_TEST_CASE(metainfo, missing_pieces);
    RUN_TEST_CASE(metainfo, invalid_pieces);
    RUN_TEST_CASE(metainfo, pieces_not_20_multiple);

}
