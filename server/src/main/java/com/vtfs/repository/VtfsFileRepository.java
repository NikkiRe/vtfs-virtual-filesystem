package com.vtfs.repository;

import com.vtfs.model.VtfsFile;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import java.util.List;
import java.util.Optional;

@Repository
public interface VtfsFileRepository extends JpaRepository<VtfsFile, Long> {
    Optional<VtfsFile> findByTokenAndParentInoAndName(String token, Long parentIno, String name);
    
    List<VtfsFile> findByTokenAndParentIno(String token, Long parentIno);
    
    Optional<VtfsFile> findByTokenAndIno(String token, Long ino);
    
    List<VtfsFile> findByTokenAndInoIn(String token, List<Long> inos);
    
    @Query("SELECT MAX(f.ino) FROM VtfsFile f WHERE f.token = :token")
    Long findMaxInoByToken(@Param("token") String token);
    
    void deleteByTokenAndIno(String token, Long ino);
    
    @Query("DELETE FROM VtfsFile f WHERE f.token = :token AND f.parentIno = :parentIno AND f.name = :name")
    @Modifying
    void deleteByTokenAndParentInoAndName(@Param("token") String token, 
                                          @Param("parentIno") Long parentIno, 
                                          @Param("name") String name);
    
    boolean existsByTokenAndParentInoAndName(String token, Long parentIno, String name);
}

