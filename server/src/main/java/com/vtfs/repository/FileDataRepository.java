package com.vtfs.repository;

import com.vtfs.model.FileData;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import java.util.List;

@Repository
public interface FileDataRepository extends JpaRepository<FileData, Long> {
    List<FileData> findByTokenAndInoOrderByOffset(String token, Long ino);
    
    @Modifying
    @Query("DELETE FROM FileData fd WHERE fd.token = :token AND fd.ino = :ino")
    void deleteByTokenAndIno(@Param("token") String token, @Param("ino") Long ino);
    
    @Query(value = "SELECT * FROM vtfs_file_data WHERE token = CAST(:token AS VARCHAR) AND ino = :ino " +
           "AND \"offset\" < :endOffset AND (\"offset\" + OCTET_LENGTH(data)) > :startOffset " +
           "ORDER BY \"offset\"", nativeQuery = true)
    List<FileData> findByTokenAndInoAndOffsetRange(
        @Param("token") String token,
        @Param("ino") Long ino,
        @Param("startOffset") Long startOffset,
        @Param("endOffset") Long endOffset
    );
}

